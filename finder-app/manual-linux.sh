#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-
SYSROOT=$("${CROSS_COMPILE}gcc" -print-sysroot)

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # TODO: Add your kernel build steps here
    make ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}" mrproper
    make ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}" defconfig
    make -j99 ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}" all
    #make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} modules
    make ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}" dtbs
fi

echo "Adding the Image in outdir"
cp "${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image" "${OUTDIR}/Image"

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# Create necessary base directories
mkdir -p "${OUTDIR}/rootfs"/{bin,sbin,etc,lib,lib64,proc,sys,dev,run,tmp,var,home}
mkdir -p "${OUTDIR}/rootfs"/usr/{bin,sbin}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # Configure busybox
    make ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}" distclean
    make ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}" defconfig
else
    cd busybox
fi

# Make and install busybox
make -j99 ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}"
make ARCH="${ARCH}" CROSS_COMPILE="${CROSS_COMPILE}" CONFIG_PREFIX="${OUTDIR}/rootfs" install

# Don't forget to change directories, somehow this is missed
cd "$OUTDIR/rootfs"

# Add library dependencies to rootfs
INTERPRETER=$(
    "${CROSS_COMPILE}readelf" -a bin/busybox |
    grep -i 'interpreter' |
    sed 's/.*Requesting program interpreter: \(.*\)]/\1/'
)
if [ -n "$INTERPRETER" ]; then
    cp "$SYSROOT$INTERPRETER" "$OUTDIR/rootfs/lib/" 
fi

BUSYBOX_LIBS=$(
    "${CROSS_COMPILE}readelf" -a bin/busybox |
    grep -i 'Shared library' |
    sed 's/.*Shared library: \[\(.*\)\]/\1/'
)

while read -r lib; do
    echo "$lib"
    [ -z "$lib" ] && continue
    LIB_PATH="$SYSROOT/lib64/$lib"
    cp "$LIB_PATH" "$OUTDIR/rootfs/lib64/"
done <<< "$BUSYBOX_LIBS"

# Make device nodes
sudo mknod -m 666 "${OUTDIR}/rootfs/dev/null" c 1 3
sudo mknod -m 600 "${OUTDIR}/rootfs/dev/console" c 5 1

# Clean and build the writer utility
cd "$FINDER_APP_DIR"
make clean
make CROSS_COMPILE="${CROSS_COMPILE}"

# Copy the finder related scripts and executables to the /home directory
# on the target rootfs
cp finder.sh finder-test.sh writer autorun-qemu.sh "${OUTDIR}/rootfs/home/"
mkdir -p "${OUTDIR}/rootfs/home/conf"
cp conf/username.txt conf/assignment.txt "${OUTDIR}/rootfs/home/conf/"

# Chown the root directory
sudo chown -R root:root "$OUTDIR/rootfs"

# Create initramfs.cpio.gz
cd "$OUTDIR/rootfs"
find . -print0 | cpio --null -ov --format=newc | gzip -9 > "$OUTDIR/initramfs.cpio.gz"
