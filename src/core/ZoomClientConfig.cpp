#include "callie/ZoomClientConfig.h"

#include "callie/ZoomBuildConfig.h"

namespace callie {

ZoomClientConfig ZoomClientConfig::builtIn()
{
    return {QString::fromLatin1(build::zoomClientId)};
}

ZoomClientConfig ZoomClientConfig::resolve(const ZoomClientConfig &builtIn,
                                           const QProcessEnvironment &environment)
{
    const QString id = environment.value(QStringLiteral("CALLIE_ZOOM_CLIENT_ID"));
    return id.isEmpty() ? builtIn : ZoomClientConfig{id};
}

ZoomClientConfig ZoomClientConfig::resolve()
{
    return resolve(builtIn(), QProcessEnvironment::systemEnvironment());
}

} // namespace callie
