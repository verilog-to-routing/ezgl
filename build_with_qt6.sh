#!/usr/bin/env bash
#
# Install Qt 6.9.3 (if not already present) and build libezgl against it.
#
# Overrides:
#   QT_VERSION  Qt version to require (default: 6.9.3)
#   QT_PREFIX   Install root (default: /opt/qt6); the actual SDK lands at
#               ${QT_PREFIX}/${QT_VERSION}/gcc_64
#
# Run as plain user when ${QT_PREFIX} is writable; otherwise sudo is invoked
# for the install steps only.
#

set -euo pipefail

QT_VERSION="${QT_VERSION:-6.9.3}"
QT_PREFIX="${QT_PREFIX:-/opt/qt6}"
QT_DIR="${QT_PREFIX}/${QT_VERSION}/gcc_64"

install_build_dependencies() {
    # Need sudo if not running as root.
    local SUDO=""
    if [[ $EUID -ne 0 ]]; then
        SUDO="sudo"
    fi

    ${SUDO} apt-get update
    ${SUDO} apt-get install -y --no-install-recommends \
        build-essential cmake make pkg-config git wget \
        python3 python3-venv \
        libxkbcommon-dev libgl-dev libegl-dev libopengl0 \
        libegl-mesa0 libgl1-mesa-dri libfontconfig1-dev \
        libglib2.0-0 libdbus-1-3 \
        ca-certificates
}

ensure_qt_installed() {
    if [[ -f "${QT_DIR}/lib/libQt6Core.so.6" ]]; then
        echo "Qt ${QT_VERSION} already present at ${QT_DIR} — skipping install."
        return
    fi

    echo "Installing Qt ${QT_VERSION} to ${QT_PREFIX} via aqtinstall..."

    # Need sudo if neither QT_PREFIX nor its parent is writable by current user.
    local SUDO=""
    if [[ ! -w "${QT_PREFIX}" ]] && [[ ! -w "$(dirname "${QT_PREFIX}")" ]]; then
        SUDO="sudo"
    fi

    # Create throwaway venv + install aqt + install Qt
    ${SUDO} python3 -m venv /opt/aqt-venv
    ${SUDO} /opt/aqt-venv/bin/pip install --no-cache-dir aqtinstall==3.3.0
    # `--modules qtshadertools` installs the qsb shader baker that libezgl's
    # RHI build needs at compile time (replaces the apt qt6-shadertools-dev
    # package, which isn't a Qt SDK dependency).
    ${SUDO} /opt/aqt-venv/bin/aqt install-qt linux desktop "${QT_VERSION}" linux_gcc_64 \
        --outputdir "${QT_PREFIX}" --modules qtshadertools
    ${SUDO} rm -rf /opt/aqt-venv  # temporary venv no longer needed

    # Verify — confirm the shared library landed and qmake6 reports the
    # expected version (qmake6 runtime needs libglib-2.0.so.0 which
    # install_build_dependencies has already installed).
    ls "${QT_DIR}/lib/libQt6Core.so.6"
    "${QT_DIR}/bin/qmake6" --version
}

install_build_dependencies
ensure_qt_installed

mkdir -p build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release -G 'Unix Makefiles' \
          -DCMAKE_PREFIX_PATH="${QT_DIR}" ..
make -j16
