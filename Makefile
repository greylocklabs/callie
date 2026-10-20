# Everyday commands. Callie's own values are here; the targets are core's, from
# core/tooling/common.mk. Run `make` to list them.

GUI_TARGET := callie-gui
CLI_TARGET := callie
GALLERY_TARGET := callie-gallery
LOG_CATEGORIES := callie.*
# A fixed week, time and size, so the same commit always renders the same pictures.
SHOT_APP := --sample --now 2026-03-18T10:40 --size 1280x840

FEDORA_DEPS := gcc-c++ cmake ninja-build qt6-qtbase-devel qt6-qtdeclarative-devel \
	qt6-qtquickcontrols2-devel qt6-qtnetworkauth-devel qtkeychain-qt6-devel \
	tomlplusplus-devel kf6-kcalendarcore-devel ftxui-devel clang-tools-extra entr nodejs npm
DEBIAN_DEPS := build-essential cmake ninja-build qt6-base-dev qt6-declarative-dev \
	qt6-declarative-dev-tools qml6-module-qtquick-controls qml6-module-qtquick-templates \
	qml6-module-qtquick-effects qml6-module-qtquick-dialogs \
	qt6-networkauth-dev qtkeychain-qt6-dev libtomlplusplus-dev libqt6sql6-sqlite \
	libkf6calendarcore-dev libftxui-dev clang-format entr nodejs npm

include core/tooling/common.mk
