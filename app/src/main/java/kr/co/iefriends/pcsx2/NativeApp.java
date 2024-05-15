package kr.co.iefriends.pcsx2;

import android.content.Context;
import android.view.Surface;

import java.io.File;
import java.lang.ref.WeakReference;

public class NativeApp {
	protected static WeakReference<Context> mContext;
	public static Context getContext() {
		return mContext.get();
	}

	private static boolean mInitialized = false;

	public static void initializeOnce(Context context) {
		mContext = new WeakReference<>(context);
		if (mInitialized) {
			return;
		}
		File externalFilesDir = context.getExternalFilesDir(null);
		if (externalFilesDir == null) {
			externalFilesDir = context.getDataDir();
		}
		initialize(externalFilesDir.getAbsolutePath());
		mInitialized = true;
	}

	public static native void initialize(String path);
	public static native String getGameTitle(String path);
	public static native String getGameSerial();
	public static native float getFPS();

	public static native void setPadButton(int index, int range, boolean iskeypressed);

	public static native void setAspectRatio(int type);
	public static native void onNativeSurfaceCreated();
	public static native void onNativeSurfaceChanged(Surface surface, int w, int h);
	public static native void onNativeSurfaceDestroyed();

	public static native boolean runVMThread(String path);

	public static native void pause();
	public static native void resume();
	public static native void shutdown();

	public static native boolean saveStateToSlot(int slot);
	public static native boolean loadStateFromSlot(int slot);
}
