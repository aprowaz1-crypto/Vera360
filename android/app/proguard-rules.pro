# Vera360 ProGuard rules

# Keep all JNI native methods
-keepclasseswithmembernames class * {
    native <methods>;
}

# Keep the NativeBridge class
-keep class com.vera360.ax360e.NativeBridge { *; }

# Keep all activities
-keep class com.vera360.ax360e.* extends android.app.Activity { *; }
