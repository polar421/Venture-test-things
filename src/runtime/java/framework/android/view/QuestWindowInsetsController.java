package android.view;
final class QuestWindowInsetsController implements WindowInsetsController {
  private int hidden, behavior, appearance;
  // Venture has no system-bar or IME inset sources yet. A controller starts with
  // no controllable types, matching an Android window whose sources have not
  // provided controls. The inset source layer can report a new mask here.
  private int controllableInsets;
  private final java.util.List<OnControllableInsetsChangedListener> controllableInsetsListeners =
      new java.util.ArrayList<>();
  public void hide(int types) {
    hidden |= types;
  }
  public void show(int types) {
    hidden &= ~types;
  }
  public void setSystemBarsBehavior(int b) {
    behavior = b;
  }
  public int getSystemBarsBehavior() {
    return behavior;
  }
  public void setSystemBarsAppearance(int a, int mask) {
    appearance = (appearance & ~mask) | (a & mask);
  }
  public int getSystemBarsAppearance() {
    return appearance;
  }
  public void addOnControllableInsetsChangedListener(OnControllableInsetsChangedListener listener) {
    java.util.Objects.requireNonNull(listener);
    controllableInsetsListeners.add(listener);
    listener.onControllableInsetsChanged(this, controllableInsets);
  }
  public void removeOnControllableInsetsChangedListener(
      OnControllableInsetsChangedListener listener) {
    java.util.Objects.requireNonNull(listener);
    controllableInsetsListeners.remove(listener);
  }
  void setControllableInsets(int types) {
    int next = types & WindowInsets.Type.all();
    if (next == controllableInsets)
      return;
    controllableInsets = next;
    for (OnControllableInsetsChangedListener listener :
        new java.util.ArrayList<>(controllableInsetsListeners))
      listener.onControllableInsetsChanged(this, next);
  }
}
