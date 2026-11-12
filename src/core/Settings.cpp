#include "callie/Settings.h"
#include "greylock/Identity.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QStandardPaths>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace callie {

namespace {

const QString kTimeFormat = u"time/format"_s;
const QString kTimeZone = u"time/zone"_s;
const QString kShowDeclined = u"events/showDeclined"_s;
const QString kDimPast = u"events/dimPast"_s;
const QString kWidenToday = u"week/widenToday"_s;
const QString kWeekStart = u"week/start"_s;
const QString kHideWeekends = u"week/hideWeekends"_s;
const QString kWeekNumbers = u"week/numbers"_s;
const QString kWorkStart = u"week/workStart"_s;
const QString kWorkEnd = u"week/workEnd"_s;
const QString kShowWorkHours = u"week/showWorkHours"_s;
const QString kDeveloperMode = u"developer/mode"_s;
// Not "general": QSettings keeps keys without a group in [General], so a group of
// that name is written as [%General] and read back as "General", losing it.
const QString kMapApp = u"places/mapApp"_s;
const QString kSearchPlaces = u"places/searchPlacesOnline"_s;
const QStringList kMapApps = {u"system"_s, u"google"_s, u"osm"_s, u"apple"_s};
const QString kVerboseLogging = u"developer/verboseLogging"_s;
const QString kViMode = u"keyboard/viMode"_s;
const QString kLeaderKey = u"keyboard/leaderKey"_s;
const QString kLeaderTimeout = u"keyboard/leaderTimeout"_s;
constexpr int kDayMinutes = 24 * 60;
const QString kHiddenCalendars = u"calendars/hidden"_s;
const QString kKnownCalendars = u"calendars/known"_s;
const QString kCollapsedAccounts = u"calendars/collapsedAccounts"_s;
const QString kCalendarLooks = u"calendars/looks"_s;
const QString kAccountNames = u"calendars/accountNames"_s;
const QString kAccountPhotos = u"calendars/accountPhotos"_s;
const QString kTheme = u"appearance/theme"_s;
const QString kView = u"view/current"_s;
const QString kLastSeenVersion = u"app/lastSeenVersion"_s;
const QString kNewEventCalendar = u"events/newEventCalendar"_s;
const QString kDefaultCalendar = u"events/defaultCalendar"_s;
const QString kVideoCallService = u"calls/service"_s;
const QString kZoomAccounts = u"calls/zoomAccounts"_s;
const QString kZoomDefault = u"calls/zoomDefault"_s;
const QString kNotify = u"reminders/notify"_s;
const QString kCallCard = u"events/callCard"_s;
const QString kReminderMinutes = u"reminders/defaultMinutes"_s;
const QString kKeepRunning = u"reminders/keepRunning"_s;
const QString kCheckForUpdates = u"updates/check"_s;
const QStringList kViews{u"day"_s, u"week"_s, u"month"_s, u"agenda"_s};

// What names this computer's accounts and calendars, or only records what it last did, goes in
// local.ini, so settings.ini can be copied to another computer as it is.
const QStringList kLocalKeys{
    kHiddenCalendars, kKnownCalendars, kCollapsedAccounts, kCalendarLooks,    kAccountNames,
    kAccountPhotos,   kView,           kLastSeenVersion,   kNewEventCalendar, kDefaultCalendar,
    kZoomAccounts,    kZoomDefault};

} // namespace

Settings::Settings(const QString &path, QObject *parent)
    : QObject(parent), m_store(path, QSettings::IniFormat),
      m_local(QFileInfo(path).dir().filePath(u"local.ini"_s), QSettings::IniFormat)
{
    load();
    rebuildTimes();
}

QSettings &Settings::storeFor(const QString &key)
{
    return kLocalKeys.contains(key) ? m_local : m_store;
}

const QSettings &Settings::storeFor(const QString &key) const
{
    return kLocalKeys.contains(key) ? m_local : m_store;
}

void Settings::put(const QString &key, const QVariant &value)
{
    storeFor(key).setValue(key, value);
}

QVariant Settings::get(const QString &key, const QVariant &fallback) const
{
    return storeFor(key).value(key, fallback);
}

bool Settings::has(const QString &key) const
{
    return storeFor(key).contains(key);
}

QStringList Settings::views()
{
    return kViews;
}

QString Settings::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + u'/' +
           greylock::identity::dirName() + u"/settings.ini"_s;
}

void Settings::load()
{
    const int format = get(kTimeFormat, 0).toInt();
    m_timeFormat = format >= 0 && format <= int(TimeFormat::TwelveHour) ? TimeFormat(format)
                                                                        : TimeFormat::Locale;
    // A hand edit or a tzdata update can leave an id this system lacks.
    const QString zone = get(kTimeZone).toString();
    m_timeZoneId = QTimeZone::isTimeZoneIdAvailable(zone.toUtf8()) ? zone : QString();
    m_showDeclined = get(kShowDeclined, true).toBool();
    m_dimPast = get(kDimPast, true).toBool();
    m_widenToday = get(kWidenToday, false).toBool();
    m_weekStart = std::clamp(get(kWeekStart, 0).toInt(), 0, 7);
    m_hideWeekends = get(kHideWeekends, false).toBool();
    m_weekNumbers = get(kWeekNumbers, false).toBool();
    m_workStart = std::clamp(get(kWorkStart, 9 * 60).toInt(), 0, kDayMinutes - 30);
    m_workEnd = std::clamp(get(kWorkEnd, 17 * 60).toInt(), m_workStart + 30, kDayMinutes);
    m_showWorkHours = get(kShowWorkHours, false).toBool();
    m_developerMode = get(kDeveloperMode, false).toBool();
    const QString mapApp = get(kMapApp).toString();
    m_mapApp = kMapApps.contains(mapApp) ? mapApp : u"system"_s;
    m_searchPlacesOnline = get(kSearchPlaces, true).toBool();
    m_verboseLogging = get(kVerboseLogging, false).toBool();
    m_viMode = get(kViMode, false).toBool();
    const QString leader = get(kLeaderKey).toString();
    m_leaderKey = kLeaderKeys.contains(leader) ? leader : u","_s;
    m_leaderTimeout = std::max(0, get(kLeaderTimeout, 5000).toInt());
    m_hiddenCalendars = get(kHiddenCalendars).toStringList();
    m_knownCalendars = get(kKnownCalendars).toStringList();
    m_collapsedAccounts = get(kCollapsedAccounts).toStringList();
    m_calendarLooks = get(kCalendarLooks).toMap();
    m_accountNames = get(kAccountNames).toMap();
    m_accountPhotos = get(kAccountPhotos).toMap();
    // A theme file in Callie's own folder is kept relative to it, so it travels with this file.
    const QString theme = get(kTheme).toString();
    m_theme = theme.endsWith(u".toml"_s) && QDir::isRelativePath(theme)
                  ? QDir::cleanPath(QFileInfo(m_store.fileName()).dir().filePath(theme))
                  : theme;
    const QString view = get(kView).toString();
    m_view = kViews.contains(view) ? view : u"week"_s;
    m_lastSeenVersion = get(kLastSeenVersion).toString();
    m_newEventCalendar = get(kNewEventCalendar).toString();
    m_defaultCalendar = get(kDefaultCalendar).toString();
    m_videoCallService = get(kVideoCallService).toString() == u"zoom" ? u"zoom"_s : u"calendar"_s;
    m_zoomAccounts = get(kZoomAccounts).toStringList();
    m_zoomDefault = get(kZoomDefault).toString();
    if (!m_zoomAccounts.contains(m_zoomDefault))
        m_zoomDefault = m_zoomAccounts.value(0);
    m_notify = get(kNotify, true).toBool();
    m_callCard = get(kCallCard, true).toBool();
    m_reminderMinutes = std::max(-1, get(kReminderMinutes, 10).toInt());
    m_keepRunning = get(kKeepRunning, false).toBool();
    m_checkForUpdates = get(kCheckForUpdates, true).toBool();
}

void Settings::setTimeFormat(TimeFormat format)
{
    if (m_timeFormat == format)
        return;
    m_timeFormat = format;
    put(kTimeFormat, int(format));
    Q_EMIT timeFormatChanged();
    rebuildTimes();
}

bool Settings::use24Hour() const
{
    switch (m_timeFormat) {
    case TimeFormat::TwentyFourHour: return true;
    case TimeFormat::TwelveHour: return false;
    case TimeFormat::Locale: break;
    }
    // Locales with a 12-hour clock put an AM/PM marker in their time format.
    return !QLocale().timeFormat(QLocale::ShortFormat).contains(u'a', Qt::CaseInsensitive);
}

void Settings::setTimeZoneId(const QString &id)
{
    // An unknown id would silently show UTC, so it means the system zone.
    const QString kept = QTimeZone::isTimeZoneIdAvailable(id.toUtf8()) ? id : QString();
    if (m_timeZoneId == kept)
        return;
    m_timeZoneId = kept;
    put(kTimeZone, kept);
    Q_EMIT timeZoneChanged();
    rebuildTimes();
}

QTimeZone Settings::timeZone() const
{
    return m_timeZoneId.isEmpty() ? QTimeZone::systemTimeZone() : QTimeZone(m_timeZoneId.toUtf8());
}

void Settings::setShowDeclined(bool show)
{
    if (m_showDeclined == show)
        return;
    m_showDeclined = show;
    put(kShowDeclined, show);
    Q_EMIT showDeclinedChanged();
}

void Settings::setDimPast(bool dim)
{
    if (m_dimPast == dim)
        return;
    m_dimPast = dim;
    put(kDimPast, dim);
    Q_EMIT dimPastChanged();
}

void Settings::setWeekStart(int day)
{
    day = std::clamp(day, 0, 7);
    if (m_weekStart == day)
        return;
    m_weekStart = day;
    put(kWeekStart, day);
    Q_EMIT weekChanged();
}

int Settings::firstDayOfWeek() const
{
    return m_weekStart == 0 ? int(QLocale().firstDayOfWeek()) : m_weekStart;
}

void Settings::setHideWeekends(bool hide)
{
    if (m_hideWeekends == hide)
        return;
    m_hideWeekends = hide;
    put(kHideWeekends, hide);
    Q_EMIT weekChanged();
}

void Settings::setWeekNumbers(bool show)
{
    if (m_weekNumbers == show)
        return;
    m_weekNumbers = show;
    put(kWeekNumbers, show);
    Q_EMIT weekChanged();
}

void Settings::setViMode(bool on)
{
    if (m_viMode == on)
        return;
    m_viMode = on;
    put(kViMode, on);
    Q_EMIT keyboardChanged();
}

void Settings::setLeaderKey(const QString &key)
{
    if (m_leaderKey == key || !kLeaderKeys.contains(key))
        return;
    m_leaderKey = key;
    put(kLeaderKey, key);
    Q_EMIT keyboardChanged();
}

void Settings::setLeaderTimeout(int ms)
{
    ms = std::max(0, ms);
    if (m_leaderTimeout == ms)
        return;
    m_leaderTimeout = ms;
    put(kLeaderTimeout, ms);
    Q_EMIT keyboardChanged();
}

void Settings::setMapApp(const QString &app)
{
    if (m_mapApp == app || !kMapApps.contains(app))
        return;
    m_mapApp = app;
    put(kMapApp, app);
    Q_EMIT placesChanged();
}

void Settings::setSearchPlacesOnline(bool on)
{
    if (m_searchPlacesOnline == on)
        return;
    m_searchPlacesOnline = on;
    put(kSearchPlaces, on);
    Q_EMIT placesChanged();
}

void Settings::setDeveloperMode(bool on)
{
    if (m_developerMode == on)
        return;
    m_developerMode = on;
    put(kDeveloperMode, on);
    Q_EMIT developerChanged();
}

void Settings::setVerboseLogging(bool on)
{
    if (m_verboseLogging == on)
        return;
    m_verboseLogging = on;
    put(kVerboseLogging, on);
    Q_EMIT developerChanged();
}

void Settings::setShowWorkHours(bool show)
{
    if (m_showWorkHours == show)
        return;
    m_showWorkHours = show;
    put(kShowWorkHours, show);
    Q_EMIT workHoursChanged();
}

void Settings::setWorkStart(int minutes)
{
    // Working hours are at least half an hour, so the end moves along if needed.
    minutes = std::clamp(minutes, 0, kDayMinutes - 30);
    if (m_workStart == minutes)
        return;
    m_workStart = minutes;
    put(kWorkStart, minutes);
    if (m_workEnd < minutes + 30) {
        m_workEnd = minutes + 30;
        put(kWorkEnd, m_workEnd);
    }
    Q_EMIT workHoursChanged();
}

void Settings::setWorkEnd(int minutes)
{
    minutes = std::clamp(minutes, 30, kDayMinutes);
    if (m_workEnd == minutes)
        return;
    m_workEnd = minutes;
    put(kWorkEnd, minutes);
    if (m_workStart > minutes - 30) {
        m_workStart = minutes - 30;
        put(kWorkStart, m_workStart);
    }
    Q_EMIT workHoursChanged();
}

void Settings::setWidenToday(bool widen)
{
    if (m_widenToday == widen)
        return;
    m_widenToday = widen;
    put(kWidenToday, widen);
    Q_EMIT widenTodayChanged();
}

void Settings::setNewEventCalendar(const QString &id)
{
    if (m_newEventCalendar == id)
        return;
    m_newEventCalendar = id;
    put(kNewEventCalendar, id);
    Q_EMIT newEventCalendarChanged();
}

void Settings::setVideoCallService(const QString &service)
{
    const QString known = service == u"zoom" ? u"zoom"_s : u"calendar"_s;
    if (m_videoCallService == known)
        return;
    m_videoCallService = known;
    put(kVideoCallService, known);
    Q_EMIT videoCallsChanged();
}

void Settings::addZoomAccount(const QString &email)
{
    if (email.isEmpty() || m_zoomAccounts.contains(email, Qt::CaseInsensitive))
        return;
    m_zoomAccounts << email;
    put(kZoomAccounts, m_zoomAccounts);
    if (m_zoomDefault.isEmpty()) {
        m_zoomDefault = email;
        put(kZoomDefault, email);
    }
    Q_EMIT videoCallsChanged();
}

void Settings::removeZoomAccount(const QString &email)
{
    if (!m_zoomAccounts.removeOne(email))
        return;
    put(kZoomAccounts, m_zoomAccounts);
    if (m_zoomDefault == email) {
        m_zoomDefault = m_zoomAccounts.value(0);
        put(kZoomDefault, m_zoomDefault);
    }
    Q_EMIT videoCallsChanged();
}

void Settings::setZoomDefault(const QString &email)
{
    if (m_zoomDefault == email || !m_zoomAccounts.contains(email))
        return;
    m_zoomDefault = email;
    put(kZoomDefault, email);
    Q_EMIT videoCallsChanged();
}

void Settings::setDefaultCalendar(const QString &id)
{
    if (m_defaultCalendar == id)
        return;
    m_defaultCalendar = id;
    put(kDefaultCalendar, id);
    Q_EMIT defaultCalendarChanged();
}

void Settings::setLastSeenVersion(const QString &version)
{
    if (m_lastSeenVersion == version)
        return;
    m_lastSeenVersion = version;
    put(kLastSeenVersion, version);
    Q_EMIT lastSeenVersionChanged();
}

void Settings::setView(const QString &view)
{
    if (m_view == view || !kViews.contains(view))
        return;
    m_view = view;
    put(kView, view);
    Q_EMIT viewChanged();
}

void Settings::setCalendarVisible(const QString &id, bool visible)
{
    // A choice made counts as having seen the calendar, so seeding leaves it.
    if (!m_knownCalendars.contains(id)) {
        m_knownCalendars.append(id);
        put(kKnownCalendars, m_knownCalendars);
    }
    if (m_hiddenCalendars.contains(id) == !visible)
        return;
    if (visible)
        m_hiddenCalendars.removeAll(id);
    else
        m_hiddenCalendars.append(id);
    put(kHiddenCalendars, m_hiddenCalendars);
    Q_EMIT hiddenCalendarsChanged();
}

void Settings::seedCalendars(const QList<CalendarInfo> &calendars)
{
    bool hidden = false;
    bool seen = false;
    for (const CalendarInfo &calendar : calendars) {
        if (m_knownCalendars.contains(calendar.id))
            continue;
        m_knownCalendars.append(calendar.id);
        seen = true;
        if (!calendar.enabled && !m_hiddenCalendars.contains(calendar.id)) {
            m_hiddenCalendars.append(calendar.id);
            hidden = true;
        }
    }
    if (seen)
        put(kKnownCalendars, m_knownCalendars);
    if (hidden) {
        put(kHiddenCalendars, m_hiddenCalendars);
        Q_EMIT hiddenCalendarsChanged();
    }
}

bool Settings::isShown(const CalendarInfo &calendar) const
{
    if (m_hiddenCalendars.contains(calendar.id))
        return false;
    return m_knownCalendars.contains(calendar.id) || calendar.enabled;
}

void Settings::setTheme(const QString &idOrPath)
{
    if (m_theme == idOrPath)
        return;
    m_theme = idOrPath;
    const QString relative = QFileInfo(m_store.fileName()).dir().relativeFilePath(idOrPath);
    put(kTheme,
        QDir::isAbsolutePath(idOrPath) && !relative.startsWith(u"../"_s) ? relative : idOrPath);
    Q_EMIT themeChanged();
}

void Settings::setAccountCollapsed(const QString &account, bool collapsed)
{
    if (m_collapsedAccounts.contains(account) == collapsed)
        return;
    if (collapsed)
        m_collapsedAccounts.append(account);
    else
        m_collapsedAccounts.removeAll(account);
    put(kCollapsedAccounts, m_collapsedAccounts);
    Q_EMIT collapsedAccountsChanged();
}

void Settings::rebuildTimes()
{
    if (m_times && m_times->zone() == timeZone() && m_times->use24Hour() == use24Hour())
        return;
    // A new object, not a mutated one, so bindings holding the old one re-run.
    if (m_times)
        m_times->deleteLater();
    m_times = new Times(timeZone(), use24Hour(), this);
    Q_EMIT timesChanged();
}

void Settings::setNotify(bool notify)
{
    if (m_notify == notify)
        return;
    m_notify = notify;
    put(kNotify, notify);
    Q_EMIT notifyChanged();
}

void Settings::setCallCard(bool show)
{
    if (m_callCard == show)
        return;
    m_callCard = show;
    put(kCallCard, show);
    Q_EMIT callCardChanged();
}

void Settings::setReminderMinutes(int minutes)
{
    minutes = std::max(-1, minutes);
    if (m_reminderMinutes == minutes)
        return;
    m_reminderMinutes = minutes;
    put(kReminderMinutes, minutes);
    Q_EMIT reminderMinutesChanged();
}

void Settings::setCheckForUpdates(bool check)
{
    if (m_checkForUpdates == check)
        return;
    m_checkForUpdates = check;
    put(kCheckForUpdates, check);
    Q_EMIT checkForUpdatesChanged();
}

void Settings::setKeepRunning(bool keep)
{
    if (m_keepRunning == keep)
        return;
    m_keepRunning = keep;
    put(kKeepRunning, keep);
    Q_EMIT keepRunningChanged();
}

QStringList Settings::availableTimeZones()
{
    QStringList ids;
    for (const QByteArray &id : QTimeZone::availableTimeZoneIds())
        ids.append(QString::fromUtf8(id));
    return ids;
}

void Settings::seedFromGoogle(const QHash<QString, QString> &google)
{
    const auto unset = [this, &google](const QString &ours, const QString &theirs) {
        return !has(ours) && google.contains(theirs);
    };
    const auto yes = [&google](const QString &id) { return google.value(id) == u"true"; };
    // Each is stored even when it matches Callie's default, so it counts as
    // set and a second account leaves it alone.
    if (unset(kWeekStart, u"weekStart"_s)) {
        // Google counts from Sunday as 0; Qt from Monday as 1.
        const int day = google.value(u"weekStart"_s).toInt();
        setWeekStart(day == 0 ? int(Qt::Sunday) : std::clamp(day, 1, 6));
        put(kWeekStart, m_weekStart);
    }
    if (unset(kHideWeekends, u"hideWeekends"_s)) {
        setHideWeekends(yes(u"hideWeekends"_s));
        put(kHideWeekends, m_hideWeekends);
    }
    if (unset(kTimeFormat, u"format24HourTime"_s)) {
        setTimeFormat(yes(u"format24HourTime"_s) ? TimeFormat::TwentyFourHour
                                                 : TimeFormat::TwelveHour);
        put(kTimeFormat, int(m_timeFormat));
    }
    if (unset(kShowDeclined, u"showDeclinedEvents"_s)) {
        setShowDeclined(yes(u"showDeclinedEvents"_s));
        put(kShowDeclined, m_showDeclined);
    }
}

void Settings::setCalendarName(const QString &id, const QString &name)
{
    QVariantMap look = m_calendarLooks.value(id).toMap();
    const QString trimmed = name.trimmed();
    if (look.value(u"name"_s).toString() == trimmed)
        return;
    if (trimmed.isEmpty())
        look.remove(u"name"_s);
    else
        look.insert(u"name"_s, trimmed);
    if (look.isEmpty())
        m_calendarLooks.remove(id);
    else
        m_calendarLooks.insert(id, look);
    put(kCalendarLooks, m_calendarLooks);
    Q_EMIT calendarLooksChanged();
}

void Settings::setCalendarColor(const QString &id, const QColor &color)
{
    QVariantMap look = m_calendarLooks.value(id).toMap();
    const QString name = color.isValid() ? color.name() : QString();
    if (look.value(u"color"_s).toString() == name)
        return;
    if (name.isEmpty())
        look.remove(u"color"_s);
    else
        look.insert(u"color"_s, name);
    if (look.isEmpty())
        m_calendarLooks.remove(id);
    else
        m_calendarLooks.insert(id, look);
    put(kCalendarLooks, m_calendarLooks);
    Q_EMIT calendarLooksChanged();
}

void Settings::resetCalendarLook(const QString &id)
{
    if (!m_calendarLooks.remove(id))
        return;
    put(kCalendarLooks, m_calendarLooks);
    Q_EMIT calendarLooksChanged();
}

void Settings::setAccountName(const QString &account, const QString &name)
{
    const QString trimmed = name.trimmed();
    if (m_accountNames.value(account).toString() == trimmed)
        return;
    if (trimmed.isEmpty())
        m_accountNames.remove(account);
    else
        m_accountNames.insert(account, trimmed);
    put(kAccountNames, m_accountNames);
    Q_EMIT accountNamesChanged();
}

void Settings::setAccountPhoto(const QString &account, const QUrl &photo)
{
    if (m_accountPhotos.value(account).toUrl() == photo)
        return;
    if (photo.isEmpty())
        m_accountPhotos.remove(account);
    else
        m_accountPhotos.insert(account, photo);
    put(kAccountPhotos, m_accountPhotos);
    Q_EMIT accountPhotosChanged();
}

QString Settings::accountName(const QString &account) const
{
    return m_accountNames.value(account, account).toString();
}

void Settings::reset()
{
    m_store.clear();
    m_local.clear();
    const TimeFormat format = m_timeFormat;
    const QString zone = m_timeZoneId;
    const bool declined = m_showDeclined, dim = m_dimPast, widen = m_widenToday;
    const QStringList hidden = m_hiddenCalendars, known = m_knownCalendars;
    const QStringList collapsed = m_collapsedAccounts;
    const QVariantMap looks = m_calendarLooks, accountNames = m_accountNames;
    const QVariantMap accountPhotos = m_accountPhotos;
    const QString theme = m_theme;
    const QString view = m_view;
    const QString defaultCalendar = m_defaultCalendar;
    const QString videoCallService = m_videoCallService;
    const QStringList zoomAccounts = m_zoomAccounts;
    const QString zoomDefault = m_zoomDefault;
    const bool notify = m_notify, keep = m_keepRunning, updates = m_checkForUpdates;
    const bool callCard = m_callCard;
    const int minutes = m_reminderMinutes;
    const int weekStart = m_weekStart, workStart = m_workStart, workEnd = m_workEnd;
    const bool showWorkHours = m_showWorkHours;
    const bool developerMode = m_developerMode, verboseLogging = m_verboseLogging;
    const QString mapApp = m_mapApp;
    const bool searchPlacesOnline = m_searchPlacesOnline;
    const bool hideWeekends = m_hideWeekends, weekNumbers = m_weekNumbers;
    const bool viMode = m_viMode;
    const QString leaderKey = m_leaderKey;
    const int leaderTimeout = m_leaderTimeout;
    load();
    // Photos are what Google says, not a choice, so they stay.
    m_accountPhotos = accountPhotos;
    if (!m_accountPhotos.isEmpty())
        put(kAccountPhotos, m_accountPhotos);
    // Connected Zoom accounts keep their sign-ins in the keyring, so they stay connected.
    m_zoomAccounts = zoomAccounts;
    m_zoomDefault = zoomDefault;
    if (!m_zoomAccounts.isEmpty()) {
        put(kZoomAccounts, m_zoomAccounts);
        put(kZoomDefault, m_zoomDefault);
    }
    if (format != m_timeFormat)
        Q_EMIT timeFormatChanged();
    if (zone != m_timeZoneId)
        Q_EMIT timeZoneChanged();
    if (declined != m_showDeclined)
        Q_EMIT showDeclinedChanged();
    if (dim != m_dimPast)
        Q_EMIT dimPastChanged();
    if (widen != m_widenToday)
        Q_EMIT widenTodayChanged();
    // Calendars no longer remembered as seen are seeded again, by whoever listens.
    if (hidden != m_hiddenCalendars || known != m_knownCalendars)
        Q_EMIT hiddenCalendarsChanged();
    if (collapsed != m_collapsedAccounts)
        Q_EMIT collapsedAccountsChanged();
    if (looks != m_calendarLooks)
        Q_EMIT calendarLooksChanged();
    if (accountNames != m_accountNames)
        Q_EMIT accountNamesChanged();
    if (theme != m_theme)
        Q_EMIT themeChanged();
    if (view != m_view)
        Q_EMIT viewChanged();
    if (defaultCalendar != m_defaultCalendar)
        Q_EMIT defaultCalendarChanged();
    if (videoCallService != m_videoCallService)
        Q_EMIT videoCallsChanged();
    if (callCard != m_callCard)
        Q_EMIT callCardChanged();
    if (notify != m_notify)
        Q_EMIT notifyChanged();
    if (minutes != m_reminderMinutes)
        Q_EMIT reminderMinutesChanged();
    if (keep != m_keepRunning)
        Q_EMIT keepRunningChanged();
    if (updates != m_checkForUpdates)
        Q_EMIT checkForUpdatesChanged();
    if (weekStart != m_weekStart || hideWeekends != m_hideWeekends || weekNumbers != m_weekNumbers)
        Q_EMIT weekChanged();
    if (workStart != m_workStart || workEnd != m_workEnd || showWorkHours != m_showWorkHours)
        Q_EMIT workHoursChanged();
    if (mapApp != m_mapApp || searchPlacesOnline != m_searchPlacesOnline)
        Q_EMIT placesChanged();
    if (developerMode != m_developerMode || verboseLogging != m_verboseLogging)
        Q_EMIT developerChanged();
    if (viMode != m_viMode || leaderKey != m_leaderKey || leaderTimeout != m_leaderTimeout)
        Q_EMIT keyboardChanged();
    rebuildTimes();
}

} // namespace callie
