#include "callie/Event.h"
#include "callie/Places.h"

#include <QTest>
#include <QUrlQuery>

using namespace callie;
using namespace Qt::StringLiterals;

class TestPlaces : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void callLinksAreFound_data();
    void callLinksAreFound();
    void mapLinksFollowTheApp();
    void joinPrefersTheLinkInWhere();
};

void TestPlaces::callLinksAreFound_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QUrl>("link");
    QTest::newRow("zoom") << u"Dial in: https://acme.zoom.us/j/123456?pwd=abc."_s
                          << QUrl(u"https://acme.zoom.us/j/123456?pwd=abc"_s);
    QTest::newRow("meet") << u"(https://meet.google.com/abc-defg-hij)"_s
                          << QUrl(u"https://meet.google.com/abc-defg-hij"_s);
    QTest::newRow("teams") << u"https://teams.microsoft.com/l/meetup-join/19%3a1/0"_s
                           << QUrl(u"https://teams.microsoft.com/l/meetup-join/19%3a1/0"_s);
    QTest::newRow("teams new link") << u"https://teams.microsoft.com/meet/123456789?p=abc"_s
                                    << QUrl(u"https://teams.microsoft.com/meet/123456789?p=abc"_s);
    QTest::newRow("webex") << u"https://acme.webex.com/meet/pat"_s
                           << QUrl(u"https://acme.webex.com/meet/pat"_s);
    QTest::newRow("first of two")
        << u"https://example.com then https://zoom.us/j/1 https://zoom.us/j/2"_s
        << QUrl(u"https://zoom.us/j/1"_s);
    QTest::newRow("simplepractice")
        << u"https://video.simplepractice.com/appt-a14743dd?origin=client"_s
        << QUrl(u"https://video.simplepractice.com/appt-a14743dd?origin=client"_s);
    QTest::newRow("doxy.me") << u"https://doxy.me/drsmith"_s << QUrl(u"https://doxy.me/drsmith"_s);
    QTest::newRow("jitsi") << u"https://meet.jit.si/OurRoom"_s
                           << QUrl(u"https://meet.jit.si/OurRoom"_s);
    QTest::newRow("a service's home page") << u"https://doxy.me/"_s << QUrl();
    QTest::newRow("not a call") << u"https://example.com/zoom and zoom.us/j/1"_s << QUrl();
    QTest::newRow("zoom elsewhere") << u"https://zoom.us/pricing"_s << QUrl();
}

void TestPlaces::callLinksAreFound()
{
    QFETCH(QString, text);
    QFETCH(QUrl, link);
    QCOMPARE(places::findCallLink(text), link);
}

void TestPlaces::mapLinksFollowTheApp()
{
    const QString place = u"Café Rosa, 12 Main St & 3rd"_s;
    const QUrl system = places::mapUrl(u"system"_s, place);
    QCOMPARE(system.scheme(), u"geo"_s);
    QCOMPARE(QUrlQuery(system).queryItemValue(u"q"_s, QUrl::FullyDecoded), place);

    const QUrl google = places::mapUrl(u"google"_s, place);
    QCOMPARE(google.host(), u"www.google.com"_s);
    QCOMPARE(QUrlQuery(google).queryItemValue(u"query"_s, QUrl::FullyDecoded), place);
    QCOMPARE(
        QUrlQuery(places::mapUrl(u"osm"_s, place)).queryItemValue(u"query"_s, QUrl::FullyDecoded),
        place);
    QCOMPARE(
        QUrlQuery(places::mapUrl(u"apple"_s, place)).queryItemValue(u"q"_s, QUrl::FullyDecoded),
        place);

    // A web address is a place already.
    QCOMPARE(places::mapUrl(u"google"_s, u" https://example.com/venue "_s),
             QUrl(u"https://example.com/venue"_s));
}

void TestPlaces::joinPrefersTheLinkInWhere()
{
    Event e;
    e.description = u"Agenda first.\nJoin: https://example.zoom.us/j/987?pwd=x."_s;
    QCOMPARE(e.joinUrl(), QUrl(u"https://example.zoom.us/j/987?pwd=x"_s));
    // The provider's call comes ahead of a link in the notes.
    e.conferenceUrl = QUrl(u"https://meet.google.com/own-call"_s);
    QCOMPARE(e.joinUrl(), e.conferenceUrl);
    // A call typed in Where is the one that was meant, even when the provider added its own.
    e.location = u"https://video.simplepractice.com/appt-a14743dd?origin=client"_s;
    QCOMPARE(e.joinUrl(), QUrl(u"https://video.simplepractice.com/appt-a14743dd?origin=client"_s));
    // Only a web link joins, whoever set it.
    e.location.clear();
    for (const QString &url : {u"file:///home/sam/run.sh"_s, u"smb://host/share"_s}) {
        e.conferenceUrl = QUrl(url);
        QCOMPARE(e.joinUrl(), QUrl(u"https://example.zoom.us/j/987?pwd=x"_s));
    }

    Event other;
    other.description = u"https://example.com/zoom"_s;
    QVERIFY(other.joinUrl().isEmpty());
}

QTEST_GUILESS_MAIN(TestPlaces)
#include "tst_places.moc"
