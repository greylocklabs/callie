#include "callie/Event.h"

#include "callie/Places.h"

namespace callie {

QUrl Event::joinUrl() const
{
    // A call link typed in Where is the call that was meant, ahead of one the provider made.
    if (const QUrl typed = places::findCallLink(location); typed.isValid())
        return typed;
    // The provider's link comes from whoever sent the invitation, so only a web
    // link is opened, never a file or share.
    if (conferenceUrl.isValid() &&
        (conferenceUrl.scheme() == u"https" || conferenceUrl.scheme() == u"http"))
        return conferenceUrl;
    return places::findCallLink(description);
}

} // namespace callie
