#define _GNU_SOURCE
#include <openxr/openxr.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>

typedef struct ActionRec {
    uintptr_t handle;
    char *name;
    struct ActionRec *next;
} ActionRec;

typedef struct SpaceRec {
    uintptr_t handle;
    const char *name;
    uint64_t sub;
    int have_pose;
    XrPosef last_pose;
    struct SpaceRec *next;
} SpaceRec;

typedef struct BoolRec {
    uintptr_t action;
    uint64_t sub;
    int have;
    XrBool32 active, current, changed;
} BoolRec;

typedef struct FloatRec {
    uintptr_t action;
    uint64_t sub;
    int have;
    XrBool32 active, changed;
    float current;
} FloatRec;

typedef struct Vec2Rec {
    uintptr_t action;
    uint64_t sub;
    int have;
    XrBool32 active, changed;
    float x, y;
} Vec2Rec;

typedef struct PoseRec {
    uintptr_t action;
    uint64_t sub;
    int have;
    XrBool32 active;
} PoseRec;

static FILE *trace_file;
static ActionRec *actions;
static SpaceRec *spaces;
static BoolRec bools[96];
static FloatRec floats[96];
static Vec2Rec vec2s[96];
static PoseRec poses[96];
static int bool_count, float_count, vec2_count, pose_count;
static int view_have[8];
static XrPosef view_last[8];
static unsigned long frame_count;

static void ensure_log(void) {
    const char *path;
    if (trace_file) return;
    path = getenv("XR_INPUT_TRACE_LOG");
    if (!path || !*path) path = "/tmp/venture-xr-input-trace.log";
    trace_file = fopen(path, "w");
    if (!trace_file) {
        trace_file = stderr;
    } else {
        setvbuf(trace_file, NULL, _IOLBF, 0);
    }
}

static void tlog(const char *fmt, ...) {
    va_list ap;
    struct timespec ts;
    double t;
    ensure_log();
    clock_gettime(CLOCK_MONOTONIC, &ts);
    t = (double)ts.tv_sec + (double)ts.tv_nsec / 1.0e9;
    fprintf(trace_file, "%.6f ", t);
    va_start(ap, fmt);
    vfprintf(trace_file, fmt, ap);
    va_end(ap);
    fputc('\n', trace_file);
    fflush(trace_file);
}

static uintptr_t hkey(const void *p) {
    return (uintptr_t)p;
}

static void remember_action(XrAction action, const char *name) {
    ActionRec *r;
    if (!action) return;
    for (r = actions; r; r = r->next) {
        if (r->handle == hkey(action)) return;
    }
    r = calloc(1, sizeof(*r));
    if (!r) return;
    r->handle = hkey(action);
    r->name = strdup(name && *name ? name : "unnamed");
    r->next = actions;
    actions = r;
}

static const char *action_name(XrAction action) {
    ActionRec *r;
    for (r = actions; r; r = r->next) {
        if (r->handle == hkey(action)) return r->name;
    }
    return "unknown";
}

static SpaceRec *space_rec(XrSpace space) {
    SpaceRec *r;
    for (r = spaces; r; r = r->next) {
        if (r->handle == hkey(space)) return r;
    }
    return NULL;
}

static void remember_space(XrSpace space, const char *name, uint64_t sub) {
    SpaceRec *r;
    if (!space) return;
    if (space_rec(space)) return;
    r = calloc(1, sizeof(*r));
    if (!r) return;
    r->handle = hkey(space);
    r->name = name ? name : "unknown";
    r->sub = sub;
    r->next = spaces;
    spaces = r;
}

static int pose_changed(const XrPosef *a, const XrPosef *b) {
    return fabsf(a->position.x - b->position.x) > 0.0001f ||
           fabsf(a->position.y - b->position.y) > 0.0001f ||
           fabsf(a->position.z - b->position.z) > 0.0001f ||
           fabsf(a->orientation.x - b->orientation.x) > 0.0001f ||
           fabsf(a->orientation.y - b->orientation.y) > 0.0001f ||
           fabsf(a->orientation.z - b->orientation.z) > 0.0001f ||
           fabsf(a->orientation.w - b->orientation.w) > 0.0001f;
}

static void log_pose(const char *kind, const char *name, uint64_t sub,
                     XrPosef *p, XrSpaceLocationFlags flags) {
    tlog("%s name=%s sub=%llu flags=0x%llx pos=(%.6f,%.6f,%.6f) quat=(%.6f,%.6f,%.6f,%.6f)",
         kind, name, (unsigned long long)sub, (unsigned long long)flags,
         p->position.x, p->position.y, p->position.z,
         p->orientation.x, p->orientation.y, p->orientation.z, p->orientation.w);
}

XRAPI_ATTR XrResult XRAPI_CALL xrPollEvent(
    XrInstance instance, XrEventDataBuffer *eventData) {
    static PFN_xrPollEvent real;
    XrResult r;
    if (!real) real = (PFN_xrPollEvent)dlsym(RTLD_NEXT, "xrPollEvent");
    if (!real) return XR_ERROR_FUNCTION_UNSUPPORTED;
    r = real(instance, eventData);
    if (XR_SUCCEEDED(r) && eventData &&
        eventData->type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
        XrEventDataSessionStateChanged *e =
            (XrEventDataSessionStateChanged *)eventData;
        tlog("SESSION_STATE state=%d time=%lld", (int)e->state,
             (long long)e->time);
    }
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateAction(
    XrActionSet actionSet, const XrActionCreateInfo *createInfo, XrAction *action) {
    static PFN_xrCreateAction real;
    XrResult r;
    if (!real) real = (PFN_xrCreateAction)dlsym(RTLD_NEXT, "xrCreateAction");
    if (!real) return XR_ERROR_FUNCTION_UNSUPPORTED;
    r = real(actionSet, createInfo, action);
    if (XR_SUCCEEDED(r) && action && *action && createInfo) {
        remember_action(*action, createInfo->actionName);
        tlog("ACTION_CREATE name=%s type=%d local=%s",
             createInfo->actionName, (int)createInfo->actionType,
             createInfo->localizedActionName);
    }
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSpace(
    XrSession session, const XrActionSpaceCreateInfo *createInfo, XrSpace *space) {
    static PFN_xrCreateActionSpace real;
    XrResult r;
    if (!real) real = (PFN_xrCreateActionSpace)dlsym(RTLD_NEXT, "xrCreateActionSpace");
    if (!real) return XR_ERROR_FUNCTION_UNSUPPORTED;
    r = real(session, createInfo, space);
    if (XR_SUCCEEDED(r) && space && *space && createInfo) {
        const char *name = action_name(createInfo->action);
        remember_space(*space, name, (uint64_t)createInfo->subactionPath);
        tlog("SPACE_CREATE name=%s sub=%llu", name,
             (unsigned long long)createInfo->subactionPath);
    }
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL xrLocateSpace(
    XrSpace space, XrSpace baseSpace, XrTime time, XrSpaceLocation *location) {
    static PFN_xrLocateSpace real;
    XrResult r;
    SpaceRec *rec;
    if (!real) real = (PFN_xrLocateSpace)dlsym(RTLD_NEXT, "xrLocateSpace");
    if (!real) return XR_ERROR_FUNCTION_UNSUPPORTED;
    r = real(space, baseSpace, time, location);
    rec = space_rec(space);
    if (XR_SUCCEEDED(r) && rec && location &&
        (location->locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
        if (!rec->have_pose || pose_changed(&rec->last_pose, &location->pose)) {
            log_pose("CONTROLLER_POSE", rec->name, rec->sub, &location->pose,
                     location->locationFlags);
            rec->last_pose = location->pose;
            rec->have_pose = 1;
        }
    }
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL xrLocateViews(
    XrSession session, const XrViewLocateInfo *viewLocateInfo,
    XrViewState *viewState, uint32_t viewCapacityInput,
    uint32_t *viewCountOutput, XrView *views) {
    static PFN_xrLocateViews real;
    XrResult r;
    uint32_t i, n;
    if (!real) real = (PFN_xrLocateViews)dlsym(RTLD_NEXT, "xrLocateViews");
    if (!real) return XR_ERROR_FUNCTION_UNSUPPORTED;
    r = real(session, viewLocateInfo, viewState, viewCapacityInput,
             viewCountOutput, views);
    if (XR_SUCCEEDED(r) && views && viewCountOutput) {
        n = *viewCountOutput < 8 ? *viewCountOutput : 8;
        for (i = 0; i < n; ++i) {
            if (!view_have[i] || pose_changed(&view_last[i], &views[i].pose)) {
                log_pose("HEAD_POSE", "view", i, &views[i].pose,
                         viewState ? viewState->viewStateFlags : 0);
                view_last[i] = views[i].pose;
                view_have[i] = 1;
            }
        }
    }
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL xrEndFrame(XrSession session, const XrFrameEndInfo *frameEndInfo) {
    static PFN_xrEndFrame real;
    XrResult r;
    if (!real) real = (PFN_xrEndFrame)dlsym(RTLD_NEXT, "xrEndFrame");
    if (!real) return XR_ERROR_FUNCTION_UNSUPPORTED;
    r = real(session, frameEndInfo);
    ++frame_count;
    tlog("FRAME n=%lu result=%d", frame_count, (int)r);
    if (XR_FAILED(r)) tlog("FRAME_ERROR n=%lu result=%d", frame_count, (int)r);
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateBoolean(
    XrSession session, const XrActionStateGetInfo *getInfo,
    XrActionStateBoolean *state) {
    static PFN_xrGetActionStateBoolean real;
    XrResult r;
    BoolRec *rec = NULL;
    int i;
    if (!real) real = (PFN_xrGetActionStateBoolean)dlsym(RTLD_NEXT, "xrGetActionStateBoolean");
    if (!real) return XR_ERROR_FUNCTION_UNSUPPORTED;
    r = real(session, getInfo, state);
    if (XR_SUCCEEDED(r) && getInfo && state) {
        for (i = 0; i < bool_count; ++i) {
            if (bools[i].action == hkey(getInfo->action) &&
                bools[i].sub == (uint64_t)getInfo->subactionPath) {
                rec = &bools[i];
                break;
            }
        }
        if (!rec && bool_count < (int)(sizeof(bools) / sizeof(bools[0]))) {
            rec = &bools[bool_count++];
            rec->action = hkey(getInfo->action);
            rec->sub = (uint64_t)getInfo->subactionPath;
        }
        if (rec && (!rec->have || rec->active != state->isActive ||
                    rec->current != state->currentState ||
                    rec->changed != state->changedSinceLastSync)) {
            tlog("ACTION_BOOL name=%s sub=%llu active=%u current=%u changed=%u",
                 action_name(getInfo->action),
                 (unsigned long long)getInfo->subactionPath,
                 (unsigned)state->isActive, (unsigned)state->currentState,
                 (unsigned)state->changedSinceLastSync);
            rec->have = 1;
            rec->active = state->isActive;
            rec->current = state->currentState;
            rec->changed = state->changedSinceLastSync;
        }
    }
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateFloat(
    XrSession session, const XrActionStateGetInfo *getInfo,
    XrActionStateFloat *state) {
    static PFN_xrGetActionStateFloat real;
    XrResult r;
    FloatRec *rec = NULL;
    int i;
    if (!real) real = (PFN_xrGetActionStateFloat)dlsym(RTLD_NEXT, "xrGetActionStateFloat");
    if (!real) return XR_ERROR_FUNCTION_UNSUPPORTED;
    r = real(session, getInfo, state);
    if (XR_SUCCEEDED(r) && getInfo && state) {
        for (i = 0; i < float_count; ++i) {
            if (floats[i].action == hkey(getInfo->action) &&
                floats[i].sub == (uint64_t)getInfo->subactionPath) {
                rec = &floats[i];
                break;
            }
        }
        if (!rec && float_count < (int)(sizeof(floats) / sizeof(floats[0]))) {
            rec = &floats[float_count++];
            rec->action = hkey(getInfo->action);
            rec->sub = (uint64_t)getInfo->subactionPath;
        }
        if (rec && (!rec->have || rec->active != state->isActive ||
                    rec->changed != state->changedSinceLastSync ||
                    fabsf(rec->current - state->currentState) > 0.0001f)) {
            tlog("ACTION_FLOAT name=%s sub=%llu active=%u current=%.6f changed=%u",
                 action_name(getInfo->action),
                 (unsigned long long)getInfo->subactionPath,
                 (unsigned)state->isActive, state->currentState,
                 (unsigned)state->changedSinceLastSync);
            rec->have = 1;
            rec->active = state->isActive;
            rec->changed = state->changedSinceLastSync;
            rec->current = state->currentState;
        }
    }
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateVector2f(
    XrSession session, const XrActionStateGetInfo *getInfo,
    XrActionStateVector2f *state) {
    static PFN_xrGetActionStateVector2f real;
    XrResult r;
    Vec2Rec *rec = NULL;
    int i;
    if (!real) real = (PFN_xrGetActionStateVector2f)dlsym(RTLD_NEXT, "xrGetActionStateVector2f");
    if (!real) return XR_ERROR_FUNCTION_UNSUPPORTED;
    r = real(session, getInfo, state);
    if (XR_SUCCEEDED(r) && getInfo && state) {
        for (i = 0; i < vec2_count; ++i) {
            if (vec2s[i].action == hkey(getInfo->action) &&
                vec2s[i].sub == (uint64_t)getInfo->subactionPath) {
                rec = &vec2s[i];
                break;
            }
        }
        if (!rec && vec2_count < (int)(sizeof(vec2s) / sizeof(vec2s[0]))) {
            rec = &vec2s[vec2_count++];
            rec->action = hkey(getInfo->action);
            rec->sub = (uint64_t)getInfo->subactionPath;
        }
        if (rec && (!rec->have || rec->active != state->isActive ||
                    rec->changed != state->changedSinceLastSync ||
                    fabsf(rec->x - state->currentState.x) > 0.0001f ||
                    fabsf(rec->y - state->currentState.y) > 0.0001f)) {
            tlog("ACTION_VEC2 name=%s sub=%llu active=%u current=(%.6f,%.6f) changed=%u",
                 action_name(getInfo->action),
                 (unsigned long long)getInfo->subactionPath,
                 (unsigned)state->isActive, state->currentState.x,
                 state->currentState.y, (unsigned)state->changedSinceLastSync);
            rec->have = 1;
            rec->active = state->isActive;
            rec->changed = state->changedSinceLastSync;
            rec->x = state->currentState.x;
            rec->y = state->currentState.y;
        }
    }
    return r;
}

XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStatePose(
    XrSession session, const XrActionStateGetInfo *getInfo,
    XrActionStatePose *state) {
    static PFN_xrGetActionStatePose real;
    XrResult r;
    PoseRec *rec = NULL;
    int i;
    if (!real) real = (PFN_xrGetActionStatePose)dlsym(RTLD_NEXT, "xrGetActionStatePose");
    if (!real) return XR_ERROR_FUNCTION_UNSUPPORTED;
    r = real(session, getInfo, state);
    if (XR_SUCCEEDED(r) && getInfo && state) {
        for (i = 0; i < pose_count; ++i) {
            if (poses[i].action == hkey(getInfo->action) &&
                poses[i].sub == (uint64_t)getInfo->subactionPath) {
                rec = &poses[i];
                break;
            }
        }
        if (!rec && pose_count < (int)(sizeof(poses) / sizeof(poses[0]))) {
            rec = &poses[pose_count++];
            rec->action = hkey(getInfo->action);
            rec->sub = (uint64_t)getInfo->subactionPath;
        }
        if (rec && (!rec->have || rec->active != state->isActive)) {
            tlog("ACTION_POSE name=%s sub=%llu active=%u",
                 action_name(getInfo->action),
                 (unsigned long long)getInfo->subactionPath,
                 (unsigned)state->isActive);
            rec->have = 1;
            rec->active = state->isActive;
        }
    }
    return r;
}
