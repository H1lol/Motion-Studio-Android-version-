# JNI entry points are looked up by their mangled Java name
# (Java_com_motionstudio_app_native_NativeEngine_nativeFoo), so neither the class
# nor its native methods may be renamed, even though nothing in Kotlin calls them
# across a language boundary R8 can see.
-keep class com.motionstudio.app.native.NativeEngine { *; }
-keepclasseswithmembernames class * {
    native <methods>;
}

# Keep the metadata Compose and Kotlin reflection read at runtime.
-keepattributes *Annotation*, Signature, InnerClasses, EnclosingMethod
