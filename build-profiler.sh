export ARCH=arm
export ANDROID_NDK="/home/name/android-ndk-r10d"
export ANDROID_TOOLCHAIN="${ANDROID_NDK}/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin"
alias agcc="${ANDROID_TOOLCHAIN}/arm-linux-androideabi-gcc --sysroot ${ANDROID_NDK}/platforms/android-9/arch-arm -mandroid"

BIN=profile_sys
C_NUM=8
agcc -fpie -pie -o ${BIN} profile_sys.c -DCPU_NUM=${C_NUM}
cp ${BIN} /share/sherry-tmp/.

