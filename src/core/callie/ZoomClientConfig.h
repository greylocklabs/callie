#pragma once

#include <QProcessEnvironment>
#include <QString>

namespace callie {

/// The Zoom Marketplace app Callie makes meetings as. It is a public client,
/// signing in with PKCE, so it has an id and no secret.
struct ZoomClientConfig
{
    QString clientId;

    [[nodiscard]] bool isValid() const { return !clientId.isEmpty(); }

    /// The app baked in by CMake, possibly empty.
    [[nodiscard]] static ZoomClientConfig builtIn();

    /// CALLIE_ZOOM_CLIENT_ID, when set, replaces the built-in app.
    [[nodiscard]] static ZoomClientConfig resolve(const ZoomClientConfig &builtIn,
                                                  const QProcessEnvironment &environment);
    [[nodiscard]] static ZoomClientConfig resolve();
};

} // namespace callie
