package android.view;
public interface WindowInsetsController {
  interface OnControllableInsetsChangedListener {
    void onControllableInsetsChanged(WindowInsetsController controller, int typeMask);
  }
  void hide(int types);
  void show(int types);
  void setSystemBarsBehavior(int behavior);
  int getSystemBarsBehavior();
  void setSystemBarsAppearance(int appearance, int mask);
  int getSystemBarsAppearance();
  void addOnControllableInsetsChangedListener(OnControllableInsetsChangedListener listener);
  void removeOnControllableInsetsChangedListener(OnControllableInsetsChangedListener listener);
}
