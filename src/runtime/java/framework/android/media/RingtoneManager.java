package android.media;

import android.net.Uri;

/** Minimal ringtone lookup surface; Venture has no system ringtone store. */
public class RingtoneManager {
  public static final int TYPE_RINGTONE = 1, TYPE_NOTIFICATION = 2, TYPE_ALARM = 4, TYPE_ALL = 7;

  /**
   * Roblox and Firebase ask for the system notification URI. Venture exposes no
   * system sound store, so the Android-equivalent "no default" result is null.
   */
  public static Uri getDefaultUri(int type) {
    if ((type & ~TYPE_ALL) != 0)
      throw new IllegalArgumentException("Invalid type: " + type);
    return null;
  }
}
