#include "ui/ChatPage.h"
#include <QMessageBox>
#include "ui/NewChatDialog.h"
#include "ui/SettingsDialog.h"
#include "ui/GroupChannelDialogs.h"
#include "ui/FolderDialog.h"
#include "ui/ContactsPanel.h"
#include "ui/PeerInfoPanel.h"
#include "ui/ChatPickerDialog.h"
#include "ui/QuickSwitcher.h"
#include "ui/ImageViewer.h"
#include "ui/ComposerEdit.h"
#include "net/Prefs.h"
#include "net/ChatCache.h"
#include "net/FileCache.h"
#include "ui/Stories.h"
#include "ui/SuperSearchDialog.h"
#include "ui/LinkPreviewBar.h"
#include "ui/VoiceMessageWidget.h"
#include "ui/RecordingBar.h"
#include "ui/EmojiPicker.h"
#include "ui/Icons.h"
#include "ui/AvatarUtil.h"
#include "ui/Checklist.h"
#include "ui/ModalOverlay.h"
#include "ui/EmptyChatGreeting.h"
#include "ui/AnimatedEmojiLabel.h"
#include "ui/TransferRing.h"
#include "ui/DownloadBar.h"
#include "net/DownloadCenter.h"
#include "ui/VideoMessageWidget.h"
#include "ui/ProfilePanel.h"
#include "net/ApiClient.h"
#include "net/WsClient.h"
#include "net/Session.h"
#include "net/VoiceRecorder.h"
#include "ui/Theme.h"

#include <QKeyEvent>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <QTextCursor>
#include <QTextDocument>
#include <QtMath>
#include <QMenu>
#include <QCheckBox>
#include <QWidgetAction>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QInputDialog>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QShortcut>
#include <QSplitter>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QToolTip>
#include <QDate>
#include <QLocale>
#include <QPixmap>
#include <QTextBoundaryFinder>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QStackedWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QFrame>
#include <QFontMetrics>
#include <QTime>
#include <QTimer>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QDir>
#include <QFile>
#include <QBuffer>
#include <QImage>
#include <QDateTime>
#include <QRegularExpression>
#include <QMap>
#include <algorithm>

namespace {

QString elide(const QString& s, const QFont& f, int px) {
    return QFontMetrics(f).elidedText(s.isEmpty() ? QString() : s, Qt::ElideRight, px);
}

// Подпись голосового с длительностью (хранится в content, т.к. сервер не отдаёт duration).
QString voiceLabel(int secs) {
    return QStringLiteral("🎤 %1:%2").arg(secs / 60).arg(secs % 60, 2, 10, QLatin1Char('0'));
}

// Является ли codepoint эмодзи (для «крупных эмодзи», как в Telegram).
bool isEmojiCp(char32_t c) {
    return (c >= 0x1F000 && c <= 0x1FAFF) ||
           (c >= 0x2600  && c <= 0x27BF)  ||
           (c >= 0x2300  && c <= 0x23FF)  ||
           (c >= 0x2B00  && c <= 0x2BFF)  ||
           (c >= 0x1F1E6 && c <= 0x1F1FF) ||
           c == 0x2934 || c == 0x2935 || c == 0x24C2 ||
           c == 0x3030 || c == 0x303D || c == 0x3297 || c == 0x3299;
}

// Если сообщение — только эмодзи (1-3), вернуть их кластеры; иначе пусто.
QStringList emojiOnlyClusters(const QString& content) {
    const QString s = content.trimmed();
    if (s.isEmpty()) return {};
    QStringList clusters;
    QTextBoundaryFinder bf(QTextBoundaryFinder::Grapheme, s);
    int pos = 0;
    while (true) {
        const int next = bf.toNextBoundary();
        if (next == -1) break;
        if (next <= pos) continue;
        const QString cl = s.mid(pos, next - pos);
        pos = next;
        if (cl.trimmed().isEmpty()) continue;
        const QList<uint> u = cl.toUcs4();
        if (u.isEmpty() || !isEmojiCp(static_cast<char32_t>(u.first()))) return {};
        clusters << cl;
        if (clusters.size() > 3) return {};
    }
    return clusters;
}

const QString kCallEventPrefix = QStringLiteral("[[XIPHER_CALL_EVENT]]");

// Подпись лога звонка (как в вебе): отклонён / без ответа / пропущенный.
QString callEventLabel(const QString& content, bool outgoing) {
    QString status;
    const QString raw = content.mid(kCallEventPrefix.length()).trimmed();
    const QJsonObject o = QJsonDocument::fromJson(raw.toUtf8()).object();
    status = o.value(QStringLiteral("status")).toString();
    if (status == QStringLiteral("rejected") || status == QStringLiteral("declined"))
        return QStringLiteral("Звонок отклонён");
    if (status == QStringLiteral("cancelled"))
        return outgoing ? QStringLiteral("Звонок отменён") : QStringLiteral("Пропущенный звонок");
    return outgoing ? QStringLiteral("Звонок без ответа") : QStringLiteral("Пропущенный звонок");
}

QJsonObject parseChecklist(const QString& content) {
    if (!content.startsWith(ChecklistProto::kPrefix)) return {};
    const QString raw = content.mid(ChecklistProto::kPrefix.length()).trimmed();
    return QJsonDocument::fromJson(raw.toUtf8()).object();
}
QJsonObject parseChecklistUpdate(const QString& content) {
    if (!content.startsWith(ChecklistProto::kUpdatePrefix)) return {};
    const QString raw = content.mid(ChecklistProto::kUpdatePrefix.length()).trimmed();
    return QJsonDocument::fromJson(raw.toUtf8()).object();
}
bool isGeoContent(const QString& c) {
    return c.startsWith(QStringLiteral("geo:")) || c.contains(QStringLiteral("yandex.")) ||
           c.contains(QStringLiteral("maps?pt=")) || c.contains(QStringLiteral("maps?ll="));
}

// Превью последнего сообщения для списка чатов: служебные маркеры протокола
// (звонки, чек-листы, гео) → человекочитаемый текст, а не сырой [[...]].
QString chatPreview(const QString& content) {
    if (content.startsWith(kCallEventPrefix)) {
        const QString raw = content.mid(kCallEventPrefix.length()).trimmed();
        const QString status = QJsonDocument::fromJson(raw.toUtf8()).object()
                                   .value(QStringLiteral("status")).toString();
        if (status == QStringLiteral("rejected") || status == QStringLiteral("declined"))
            return QStringLiteral("Звонок отклонён");
        if (status == QStringLiteral("cancelled"))
            return QStringLiteral("Звонок отменён");
        return QStringLiteral("Пропущенный звонок");
    }
    if (content.startsWith(ChecklistProto::kPrefix) ||
        content.startsWith(ChecklistProto::kUpdatePrefix))
        return QStringLiteral("Чек-лист");
    if (content.startsWith(QStringLiteral("geo:")))
        return QStringLiteral("Геопозиция");
    return content;
}

// Человекочитаемый размер файла.
// ── MessageTextLabel: rich-text метка с ТОЧНОЙ высотой.
// QLabel с RichText+wordWrap врёт высоту в layout'е (зависит от шрифтов/платформы)
// — из-за этого бабблы «плывут». Здесь высота считается через QTextDocument
// при фактической ширине и фиксируется: раскладка становится детерминированной.
class MessageTextLabel : public QLabel {
public:
    MessageTextLabel(const QString& html, QWidget* parent) : QLabel(parent) {
        setTextFormat(Qt::RichText);
        setWordWrap(true);
        setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
        setOpenExternalLinks(true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        // Rich-text QLabel тянет минимальную ширину layout'а на ширину
        // развёрнутого текста (длинные URL/base64) — контейнер становится
        // шире вьюпорта, бабблы «уезжают» за край. Разрешаем сжиматься.
        setMinimumWidth(1);
        // Документ парсится ОДИН раз; при ресайзе меняется только ширина
        // (дёшево). Иначе — HTML-парс на каждый пиксель ресайза на каждый
        // баббл = фриз окна.
        doc_.setDefaultFont(font());
        doc_.setDocumentMargin(0);
        doc_.setHtml(html);
        setText(html);
    }

protected:
    void resizeEvent(QResizeEvent* e) override {
        if (width() != lastW_) {
            lastW_ = width();
            doc_.setTextWidth(qMax(1, width()));
            setFixedHeight(qCeil(doc_.size().height()) + 2);
        }
        QLabel::resizeEvent(e);
    }

private:
    QTextDocument doc_;
    int lastW_ = -1;
};

static QString formatMessageHtml(const QString& raw, const QString& highlight = QString());

static QString escapeHtmlMin_(const QString& s) {
    QString r = s;
    r.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    r.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    r.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    return r;
}

// Подсветка кода: без внешних зависимостей — строки/числа/комментарии/
// ключевые слова цветами темы. Работает по уже-экранированному тексту.
static QString highlightCode(QString code) {
    static const QStringList keywords = {
        QStringLiteral("int"), QStringLiteral("float"), QStringLiteral("double"),
        QStringLiteral("char"), QStringLiteral("bool"), QStringLiteral("void"),
        QStringLiteral("const"), QStringLiteral("static"), QStringLiteral("class"),
        QStringLiteral("struct"), QStringLiteral("return"), QStringLiteral("if"),
        QStringLiteral("else"), QStringLiteral("for"), QStringLiteral("while"),
        QStringLiteral("switch"), QStringLiteral("case"), QStringLiteral("break"),
        QStringLiteral("new"), QStringLiteral("delete"), QStringLiteral("true"),
        QStringLiteral("false"), QStringLiteral("null"), QStringLiteral("nullptr"),
        QStringLiteral("public"), QStringLiteral("private"), QStringLiteral("var"),
        QStringLiteral("let"), QStringLiteral("function"), QStringLiteral("def"),
        QStringLiteral("import"), QStringLiteral("from"), QStringLiteral("async"),
        QStringLiteral("await"), QStringLiteral("template"), QStringLiteral("typename"),
        QStringLiteral("namespace"), QStringLiteral("using"), QStringLiteral("auto"),
    };
    // Комментарии (// и #) целиком до конца строки.
    static const QRegularExpression commentRe(
        QStringLiteral("(//[^\n]*|#[^\n]*)"));
    code.replace(commentRe, QStringLiteral(
        "<span style=\"color:#6C6785;\"></span>"));
    // Строки в кавычках.
    static const QRegularExpression strRe(QStringLiteral(
        "(\"[^\"\n]*\"|'[^'\n]*')"));
    code.replace(strRe, QStringLiteral(
        "<span style=\"color:#A5D6A7;\"></span>"));
    // Числа.
    static const QRegularExpression numRe(QStringLiteral("\b(\d+\.?\d*)\b"));
    code.replace(numRe, QStringLiteral(
        "<span style=\"color:#F5C451;\"></span>"));
    // Ключевые слова.
    for (const QString& kw : keywords) {
        static const QRegularExpression kwRe(
            QStringLiteral("\b(%1)\b").arg(kw));
        code.replace(kwRe, QStringLiteral(
            "<span style=\"color:#C792EA;\"></span>"));
    }
    return code;
}

QString formatMessageHtml(const QString& raw, const QString& highlight) {
    QString s = escapeHtmlMin_(raw);

    // Подсветка активного поискового запроса (SRC-04, как <mark>/ss-mark веба):
    // на экранированном тексте ДО markdown-разметки — теги разметки не задеваются.
    if (highlight.size() >= 2) {
        const QRegularExpression re(QRegularExpression::escape(highlight),
                                    QRegularExpression::CaseInsensitiveOption);
        s.replace(re, QStringLiteral(
            "<span style=\"background:rgba(139,92,246,0.45);border-radius:3px;\">\\0</span>"));
    }

    // Многострочные код-блоки ```…``` — раньше остальных форматов:
    // содержимое вынимается в токены и возвращается стилизованным <pre>.
    QMap<QString, QString> codeBlocks;
    int codeIdx = 0;
    {
        int pos = 0;
        while (true) {
            const int open = s.indexOf(QStringLiteral("```"), pos);
            if (open < 0) break;
            const int close = s.indexOf(QStringLiteral("```"), open + 3);
            const int end = close < 0 ? s.size() : close;
            QString body = s.mid(open + 3, end - open - 3);
            // возможная подпись языка на первой строке
            QString lang;
            const int nl = body.indexOf(QLatin1Char('\n'));
            if (nl > 0) {
                const QString first = body.left(nl).trimmed();
                if (!first.isEmpty() && !first.contains(QLatin1Char(' '))
                    && first.size() <= 12) {
                    lang = first;
                    body = body.mid(nl + 1);
                }
            }
            const QString token = QStringLiteral("\x01C%1\x01").arg(codeIdx++);
            const QString pre = QStringLiteral(
                "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">"
                "<tr><td style=\"background:#0B0A0E;border:1px solid #221F2C;"
                "border-radius:6px;font-family:'JetBrains Mono','Consolas',monospace;"
                "font-size:13px;white-space:pre-wrap;padding:8px 10px;\">%1</td></tr></table>")
                .arg(highlightCode(body));
            codeBlocks.insert(token, pre);
            s.replace(open, end - open + (close < 0 ? 0 : 3), token);
            pos = open + token.size();
        }
    }

    // Ссылки — раньше остальных, прячем в токены, чтобы разметка их не трогала.
    static const QRegularExpression urlRe(QStringLiteral("(https?://[^\\s<]+)"));
    QMap<QString, QString> links;
    int linkIdx = 0;
    auto it = urlRe.globalMatch(s);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString token = QStringLiteral("\x01L%1\x01").arg(linkIdx++);
        links.insert(token, m.captured(1));
        s.replace(m.captured(1), token);
    }

    s.replace(QStringLiteral("||"), QStringLiteral("\x02S\x02"));   // спойлер-маркер
    s.replace(QStringLiteral("**"), QStringLiteral("\x02B\x02"));
    s.replace(QStringLiteral("__"), QStringLiteral("\x02U\x02"));
    s.replace(QStringLiteral("~~"), QStringLiteral("\x02K\x02"));
    s.replace(QStringLiteral("`"),  QStringLiteral("\x02M\x02"));

    // Курсив — одиночная *, но не наш двойной маркер (после замены ** уже уехал).
    static const QRegularExpression italicRe(QStringLiteral("(^|[^\\x02B])(\\*)([^\\x02]+)(\\*)"));
    s.replace(italicRe, QStringLiteral("\\1<i>\\3</i>"));

    auto wrap = [](QString& str, const QString& mark, const QString& open, const QString& close) {
        const QStringList parts = str.split(mark);
        QString out;
        bool inside = false;
        for (int i = 0; i < parts.size(); ++i) {
            out += parts[i];
            if (i + 1 < parts.size()) { out += inside ? close : open; inside = !inside; }
        }
        str = out;
    };
    wrap(s, QStringLiteral("\x02S\x02"),
         QStringLiteral("<span style=\"background-color:#0B0A0E;color:#0B0A0E;border-radius:3px;\" title=\"Спойлер — выделите, чтобы прочитать\">"),
         QStringLiteral("</span>"));
    wrap(s, QStringLiteral("\x02B\x02"), QStringLiteral("<b>"), QStringLiteral("</b>"));
    wrap(s, QStringLiteral("\x02U\x02"), QStringLiteral("<u>"), QStringLiteral("</u>"));
    wrap(s, QStringLiteral("\x02K\x02"), QStringLiteral("<s>"), QStringLiteral("</s>"));
    wrap(s, QStringLiteral("\x02M\x02"),
         QStringLiteral("<span style=\"font-family:'JetBrains Mono','Consolas',monospace;background:rgba(255,255,255,0.07);border-radius:4px;\">"),
         QStringLiteral("</span>"));

    // Вернуть ссылки и код-блоки уже тегами.
    for (auto it2 = links.cbegin(); it2 != links.cend(); ++it2)
        s.replace(it2.key(), QStringLiteral("<a href=\"%1\">%1</a>").arg(it2.value()));
    for (auto it3 = codeBlocks.cbegin(); it3 != codeBlocks.cend(); ++it3)
        s.replace(it3.key(), it3.value());
    return s;
}


QString humanSize(long long bytes) {
    if (bytes <= 0) return QString();
    const char* u[] = {"Б", "КБ", "МБ", "ГБ"};
    double v = double(bytes);
    int i = 0;
    while (v >= 1024.0 && i < 3) { v /= 1024.0; ++i; }
    return QStringLiteral("%1 %2").arg(v, 0, 'f', (i == 0 ? 0 : 1)).arg(QString::fromUtf8(u[i]));
}

// Метка дня для разделителя (Сегодня / Вчера / 13 июня).
QString dateLabel(const QString& createdAt) {
    const QDate d = QDate::fromString(createdAt.left(10), QStringLiteral("yyyy-MM-dd"));
    if (!d.isValid()) return QString();
    const QDate today = QDate::currentDate();
    if (d == today) return QStringLiteral("Сегодня");
    if (d == today.addDays(-1)) return QStringLiteral("Вчера");
    const QLocale ru(QLocale::Russian);
    return ru.toString(d, d.year() == today.year() ? QStringLiteral("d MMMM")
                                                    : QStringLiteral("d MMMM yyyy"));
}

// Папка загрузок файлов: «Загрузки/Xipher Desktop» (как у Telegram Desktop).
static QString downloadsDir() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (dir.isEmpty()) dir = QDir::homePath() + QStringLiteral("/Downloads");
    dir += QStringLiteral("/Xipher Desktop");
    QDir().mkpath(dir);
    return dir;
}

// Центрированная «пилюля»-разделитель дат.
QWidget* makeDateSeparator(const QString& label) {
    auto* row = new QWidget();
    row->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* l = new QHBoxLayout(row);
    l->setContentsMargins(0, 10, 0, 6);
    l->addStretch();
    auto* pill = new QLabel(label);
    pill->setStyleSheet(QStringLiteral(
        "background:rgba(255,255,255,0.07);color:#ACA6BD;font-size:13px;font-weight:600;"
        "padding:4px 14px;border-radius:12px;"));
    l->addWidget(pill);
    l->addStretch();
    return row;
}

// Достаёт секунды из подписи вида "🎤 m:ss" (0 — если нет).
int parseVoiceSeconds(const QString& content) {
    QRegularExpression re(QStringLiteral("(\\d+):(\\d{2})"));
    auto m = re.match(content);
    if (!m.hasMatch()) return 0;
    return m.captured(1).toInt() * 60 + m.captured(2).toInt();
}

// Круглый аватар: картинка по url (если есть) либо буква на градиенте.
QLabel* makeAvatar(const QString& url, const QString& text, int size) {
    auto* a = new QLabel();
    Avatar::setRound(a, url, text, size);
    return a;
}

} // namespace


// Цвета бабблов зависят от активной темы («Оформление» в настройках).
static QString bubbleOutQss() {
    return QStringLiteral(
        "#bubbleOut{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,"
        "stop:0 #4A3A72,stop:1 #3A2D5C);border-radius:18px;}");
}
static QString bubbleInQss() {
    return QStringLiteral(
        "#bubbleIn{background:%1;border:1px solid rgba(255,255,255,0.07);"
        "border-radius:18px;}")
        .arg(ThemePreset::current().surface2.name());
}
static QString chatQSS();   // единый шаблон стилей чата (определён ниже)

ChatPage::ChatPage(ApiClient* api, WsClient* ws, QWidget* parent)
    : QWidget(parent), api_(api), ws_(ws) {
    // «Печатает…»: гасится через 4 с молчания, свой статус троттлится 5 с.
    typingTimer_ = new QTimer(this);
    typingTimer_->setSingleShot(true);
    typingTimer_->setInterval(4000);
    connect(typingTimer_, &QTimer::timeout, this, [this]() {
        if (!peerStatusBase_.isEmpty()) peerStatus_->setText(peerStatusBase_);
    });
    connect(ws_, &WsClient::typingReceived, this,
            [this](const QString& chatId, const QString& from, bool on) {
        if (from == Session::instance().userId) return;   // своё эхо
        showTyping(chatId, on);
    });
    {
        auto* typingThrottle = new QTimer(this);
        typingThrottle->setSingleShot(true);
        typingThrottle->setInterval(5000);
        connect(typingThrottle, &QTimer::timeout, this, [this]() { typingSent_ = false; });
        connect(composer_, &ComposerEdit::textChanged, this, [this, typingThrottle]() {
            saveDraft();
            if (currentPeerId_.isEmpty() || typingSent_) return;
            typingSent_ = true;
            ws_->sendTyping(currentKind_ == ChatKind::Group ? QStringLiteral("group")
                                                            : QStringLiteral("chat"),
                            currentPeerId_, true);
            typingThrottle->start();
        });
    }

    // Правка (обе стороны) и закрепление.
    connect(ws_, &WsClient::messageEdited, this,
            [this](const QString& messageId, const QString& content) {
        if (ChatMessage* m = findMessage(messageId)) {
            m->content = content;
            m->edited = true;
            rerenderPreservingScroll();
        }
    });
    connect(ws_, &WsClient::messagePinned, this,
            [this](const QString& messageId, bool pinned) {
        if (!pinned) { clearPinnedMessage(); return; }
        if (const ChatMessage* m = findMessage(messageId))
            setPinnedMessage(messageId, m->content);
        else setPinnedMessage(messageId, QStringLiteral("сообщение"));
    });

    // Реакции собеседника: эхо reaction_update (приходит и за свои — сверка).
    connect(ws_, &WsClient::reactionUpdated, this,
            [this](const QString& messageId, const QString& emoji,
                   const QString& userId, const QString& action) {
        ChatMessage* m = findMessage(messageId);
        if (!m) return;
        const bool own = (userId == Session::instance().userId);
        const bool added = (action == QStringLiteral("added"));
        bool changed = false;
        Reaction* found = nullptr;
        for (Reaction& r : m->reactions)
            if (r.emoji == emoji) { found = &r; break; }
        if (added) {
            if (!found) { m->reactions.append(Reaction{emoji, 1, own}); changed = true; }
            else if (own && !found->mine) { found->mine = true; changed = true; }
            else if (!own) { ++found->count; changed = true; }
        } else if (found) {
            if (own && found->mine) { found->mine = false; changed = true; }
            else if (!own && --found->count <= 0) { m->reactions.removeOne(*found); changed = true; }
            else if (!own) changed = true;
        }
        if (changed) refreshReactionChips(messageId);
    });

    recorder_ = new VoiceRecorder(this);
    player_   = new QMediaPlayer(this);
    audioOut_ = new QAudioOutput(this);
    player_->setAudioOutput(audioOut_);
    searchTimer_ = new QTimer(this);
    searchTimer_->setSingleShot(true);
    searchTimer_->setInterval(350);

    buildUi();

    connect(searchTimer_, &QTimer::timeout, this, [this]() {
        if (!searchQuery_.isEmpty()) api_->searchUsers(searchQuery_);
    });
    connect(api_, &ApiClient::usersFound, this,
            [this](const QString& query, const QList<UserHit>& users) {
        if (query != searchQuery_) return;   // устаревший/чужой ответ
        searchHits_ = users;
        rebuildChatList();
    });
    // Глобальный поиск: публичные каналы/группы по запросу (как в вебе —
    // каталог прямо в результатах поиска сайдбара).
    connect(api_, &ApiClient::directoryLoaded, this,
            [this](const QList<DirectoryItem>& items, bool) {
        if (searchQuery_.isEmpty()) {
            if (!directoryHits_.isEmpty()) { directoryHits_.clear(); rebuildChatList(); }
            return;
        }
        directoryHits_ = items;
        rebuildChatList();
    });
    // Присоединение к публичному чату (из сайдбара/каталога) → обновить списки.
    connect(api_, &ApiClient::publicJoined, this, [this](bool ok, const QString& id) {
        if (!ok || id != pendingJoinId_) return;
        pendingJoinId_.clear();
        api_->getChats();
        api_->getGroups();
        api_->getChannels();
    });
    // Боты: ответ на callback-кнопку и контекст MiniApp.
    connect(api_, &ApiClient::botCallbackDone, this, &ChatPage::onBotCallbackDone);
    connect(api_, &ApiClient::miniappInitReady, this, &ChatPage::onMiniappInitReady);

    connect(api_, &ApiClient::chatsLoaded,    this, &ChatPage::onChatsLoaded);
    connect(api_, &ApiClient::messagesLoaded,  this, &ChatPage::onMessagesLoaded);
    connect(api_, &ApiClient::chatActionDone, this, [this](bool ok){
        if (ok) { api_->getChats(); api_->getGroups(); api_->getChannels(); } });
    connect(api_, &ApiClient::peerActionDone, this, [this](bool ok, const QString&){
        if (ok) { api_->getGroups(); api_->getChannels(); } });
    connect(api_, &ApiClient::groupsLoaded, this, [this](const QList<Chat>& g){ groupChats_ = g; mergeAllChats(); });
    connect(api_, &ApiClient::channelsLoaded, this, [this](const QList<Chat>& c){ channelChats_ = c; mergeAllChats(); });
    connect(api_, &ApiClient::groupMessagesLoaded, this, &ChatPage::onMessagesLoaded);
    connect(api_, &ApiClient::channelMessagesLoaded, this, &ChatPage::onMessagesLoaded);
    connect(api_, &ApiClient::topicsLoaded, this, [this](const QString& gid, bool forum, const QList<Topic>& topics) {
        if (gid != currentPeerId_ || currentKind_ != ChatKind::Group) return;
        if (!currentTopicId_.isEmpty()) { currentTopics_ = topics; return; }   // внутри темы — не дёргаем вид
        currentForum_ = forum;
        if (forum) convStack_->setCurrentIndex(2), showTopicsList(topics);
        else { convStack_->setCurrentIndex(1); clearMessages(); renderCached(); api_->getGroupMessages(gid); }
    });
    connect(api_, &ApiClient::topicMessagesLoaded, this, [this](const QString& tid, const QList<ChatMessage>& msgs) {
        if (tid != currentTopicId_) return;
        applyMessages(msgs);
    });
    connect(api_, &ApiClient::topicActionDone, this, [this](bool ok, const QString&) {
        if (!ok) return;
        if (currentKind_ == ChatKind::Group && currentTopicId_.isEmpty())
            api_->getGroupTopics(currentPeerId_);   // обновить список тем
    });
    connect(api_, &ApiClient::foldersLoaded, this, [this](const QList<Folder>& f) {
        folders_ = f;
        const QString saved = Prefs::getStr(QStringLiteral("xipher_active_folder"), QStringLiteral("all"));
        activeFolderId_ = QStringLiteral("all");
        for (const Folder& fl : folders_) if (fl.id == saved) activeFolderId_ = saved;
        rebuildFolderStrip();
        rebuildChatList();
    });
    connect(api_, &ApiClient::messageSent,     this, &ChatPage::onMessageSent);
    connect(api_, &ApiClient::messageDeleted,  this, [this](bool ok){ if (ok) reloadCurrentMessages(); });
    connect(ws_,  &WsClient::newMessage,       this, &ChatPage::onWsMessage);
    // Дебаунс перезагрузки истории группы/канала (не дёргаем API на каждый пакет).
    peerReloadTimer_ = new QTimer(this);
    peerReloadTimer_->setSingleShot(true);
    peerReloadTimer_->setInterval(400);
    connect(peerReloadTimer_, &QTimer::timeout, this, [this]() { reloadCurrentMessages(); });
    connect(ws_,  &WsClient::peerMessage, this, [this](const QString& chatId) {
        if (chatId == currentPeerId_ &&
            (currentKind_ == ChatKind::Group || currentKind_ == ChatKind::Channel)) {
            peerReloadTimer_->start();
        } else {
            // Не открытый сейчас канал/группа — подсветим в списке и уведомим.
            const int idx = indexOfChat(chatId);
            if (idx >= 0) {
                chats_[idx].unread += 1;
                rebuildChatList();
                emit notify(chats_[idx].displayName, QStringLiteral("Новое сообщение"));
            }
        }
    });
    connect(api_, &ApiClient::voiceUploaded,   this, &ChatPage::onVoiceUploaded);
    connect(api_, &ApiClient::fileFetched,     this, &ChatPage::onFileFetched);
    // История не загрузилась (сеть/сервер): прекращаем «грузимся» — приветствие
    // покажется, только если чат действительно пуст, без мелькания до ответа.
    connect(api_, &ApiClient::chatError, this, [this](const QString& ctx, const QString&) {
        loadingChat_ = false;
        // Офлайн: если из кэша уже что-то показано — НЕ затираем, помечаем статус.
        if (bubbleCount_ > 0 && !currentPeerId_.isEmpty())
            peerStatus_->setText(QStringLiteral("офлайн · показана сохранённая переписка"));
        updateGreeting();
        Q_UNUSED(ctx);
    });
    // Файл голосового недоступен (404 — сервер чистит старые) → честно на виджете.
    connect(api_, &ApiClient::fileProgress, this, [this](const QString& p, qint64 rec, qint64 tot) {
        if (p == pendingPlayPath_ && tot <= 0 && rec == 0 && activeVoice_) {
            pendingPlayPath_.clear();
            activeVoice_->setUnavailable(QStringLiteral("Файл недоступен"));
        }
        // Неудачная загрузка: убираем «хвосты» ожидания — иначе карта
        // pending* растёт весь сеанс (утечка памяти по словарю).
        if (tot <= 0 && rec == 0) {
            pendingImage_.remove(p);
            pendingFileOpen_.remove(p);
        }
    });
    connect(recorder_, &VoiceRecorder::recordingFinished, this, &ChatPage::onVoiceRecorded);
    connect(recorder_, &VoiceRecorder::error, this, [this](const QString&) {
        cancelRecording();
    });

    // Прогресс воспроизведения → активный голосовой виджет.
    connect(player_, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
        if (!activeVoice_) return;
        const qint64 dur = player_->duration();
        activeVoice_->setProgress(dur > 0 ? qreal(pos) / dur : 0.0);
        activeVoice_->setElapsedMs(pos);
    });
    connect(player_, &QMediaPlayer::durationChanged, this, [this](qint64 dur) {
        if (activeVoice_) activeVoice_->setTotalMs(dur);
    });
    connect(player_, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus s) {
        if (s == QMediaPlayer::EndOfMedia && activeVoice_) {
            activeVoice_->setPlaying(false);
            activeVoice_->setProgress(0.0);
            activeVoice_->setElapsedMs(0);
        }
    });
    connect(player_, &QMediaPlayer::errorOccurred, this, [this](int, const QString& err) {
        if (activeVoice_) activeVoice_->setUnavailable(err);
    });
}

void ChatPage::buildUi() {
    setStyleSheet(chatQSS());
    setAcceptDrops(true);   // drag-n-drop файлов в чат (MLT-06)

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Сайдбар ──────────────────────────────────────────────────────────────
    // Ширина и структура 1:1 с вебом: сайдбар 380px, внутри — вертикальный
    // рейл папок 72px слева от списка чатов (chat.html .chats-body).
    sidebar_ = new QWidget(this);
    sidebar_->setObjectName(QStringLiteral("sidebar"));
    sidebar_->setFixedWidth(380);
    auto* sidebar = sidebar_;
    auto* side = new QVBoxLayout(sidebar);
    side->setContentsMargins(0, 0, 0, 0);
    side->setSpacing(0);

    auto* sideHeader = new QWidget(sidebar);
    sideHeader->setObjectName(QStringLiteral("sideHeader"));
    sideHeader->setFixedHeight(60);
    auto* shl = new QHBoxLayout(sideHeader);
    shl->setContentsMargins(8, 0, 8, 0);
    shl->setSpacing(4);
    const QColor sideIcon(0xAC, 0xA6, 0xBD);

    menuBtn_ = new QPushButton(sideHeader);
    menuBtn_->setObjectName(QStringLiteral("hdrBtn"));
    menuBtn_->setCursor(Qt::PointingHandCursor);
    menuBtn_->setIcon(Icons::icon(Icons::Menu, 22, sideIcon));
    menuBtn_->setIconSize(QSize(22, 22));
    menuBtn_->setToolTip(QStringLiteral("Меню"));

    auto* brand = new QLabel(QStringLiteral("Xipher"), sideHeader);
    brand->setObjectName(QStringLiteral("brandTitle"));

    auto* newChatBtn = new QPushButton(sideHeader);
    newChatBtn->setObjectName(QStringLiteral("hdrBtn"));
    newChatBtn->setCursor(Qt::PointingHandCursor);
    newChatBtn->setIcon(Icons::icon(Icons::Pencil, 20, sideIcon));
    newChatBtn->setIconSize(QSize(20, 20));
    newChatBtn->setToolTip(QStringLiteral("Новый чат"));

    shl->addWidget(menuBtn_);
    shl->addSpacing(2);
    shl->addWidget(brand);
    shl->addStretch();
    shl->addWidget(newChatBtn);

    auto* searchWrap = new QWidget(sidebar);
    auto* swl = new QHBoxLayout(searchWrap);
    // Отступы как .chats-search веба: 0.75rem 1.5rem. Справа — кнопка «Каталог»
    // (в вебе каталог открывается кнопкой в поисковой строке чата).
    swl->setContentsMargins(16, 12, 16, 12);
    swl->setSpacing(8);
    search_ = new QLineEdit(searchWrap);
    search_->setObjectName(QStringLiteral("searchBox"));
    search_->setPlaceholderText(QStringLiteral("Поиск"));
    swl->addWidget(search_, 1);
    auto* catalogBtn = new QPushButton(QStringLiteral("Каталог"), searchWrap);
    catalogBtn->setObjectName(QStringLiteral("catalogBtn"));
    catalogBtn->setCursor(Qt::PointingHandCursor);
    catalogBtn->setToolTip(QStringLiteral("Каталог публичных каналов и групп"));
    connect(catalogBtn, &QPushButton::clicked, this, &ChatPage::openCatalog);
    swl->addWidget(catalogBtn);

    // Сторис-бар (как .stories-bar веба): кольца-плитки над списком чатов.
    storiesBar_ = new StoriesBar(sidebar);
    storiesBar_->setObjectName(QStringLiteral("storiesBar"));

    // Рейл папок (как в Telegram/вебе): вертикальная полоса слева от списка
    // чатов, плитка = иконка 38px + подпись, бейдж непрочитанных на углу.
    folderRail_ = new QWidget(sidebar);
    folderRail_->setObjectName(QStringLiteral("folderRail"));
    folderRail_->setFixedWidth(72);
    auto* frl = new QVBoxLayout(folderRail_);
    frl->setContentsMargins(0, 12, 0, 12);
    frl->setSpacing(6);
    folderRailScroll_ = new QScrollArea(folderRail_);
    folderRailScroll_->setObjectName(QStringLiteral("folderRailScroll"));
    folderRailScroll_->setWidgetResizable(true);
    folderRailScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    folderRailScroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    folderRailScroll_->setFrameShape(QFrame::NoFrame);
    auto* railW = new QWidget();
    railW->setStyleSheet(QStringLiteral("background:transparent;"));
    folderRailItems_ = new QVBoxLayout(railW);
    folderRailItems_->setContentsMargins(0, 0, 0, 8);
    folderRailItems_->setSpacing(6);
    folderRailItems_->addStretch();
    folderRailScroll_->setWidget(railW);
    frl->addWidget(folderRailScroll_, 1);
    folderRail_->setVisible(false);   // показываем, только если есть папки

    // Список чатов (правее рейла) + рамка, как .chats-body в вебе.
    auto* chatsBody = new QWidget(sidebar);
    chatsBody->setObjectName(QStringLiteral("chatsBody"));
    auto* cbl2 = new QHBoxLayout(chatsBody);
    cbl2->setContentsMargins(0, 0, 0, 0);
    cbl2->setSpacing(0);

    chatList_ = new QListWidget(chatsBody);
    chatList_->setObjectName(QStringLiteral("chatList"));
    chatList_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    cbl2->addWidget(folderRail_);
    cbl2->addWidget(chatList_, 1);

    side->addWidget(sideHeader);
    side->addWidget(searchWrap);
    side->addWidget(storiesBar_);
    side->addWidget(chatsBody, 1);

    // ── Область переписки ──────────────────────────────────────────────────────
    convStack_ = new QStackedWidget(this);
    // Не даём внутренним минимумам переписки держать окно широким:
    // настоящий минимум (680×480) задаёт MainWindow.
    convStack_->setMinimumSize(QSize(1, 1));   // (0,0) в Qt = «не задано»

    // Пустое состояние 1:1 с .empty-chat веба: крупная иконка + заголовок + подсказка.
    emptyPage_ = new QWidget(convStack_);
    auto* empty = emptyPage_;
    empty->setStyleSheet(QStringLiteral("background:#0B0A0E;"));
    auto* el = new QVBoxLayout(empty);
    el->setContentsMargins(32, 32, 32, 32);
    el->addStretch();
    auto* emptyIcon = new QLabel(QStringLiteral("💬"), empty);
    emptyIcon->setAlignment(Qt::AlignCenter);
    emptyIcon->setStyleSheet(QStringLiteral("font-size:64px;opacity:0.5;"));
    el->addWidget(emptyIcon);
    auto* emptyTitle = new QLabel(QStringLiteral("Выберите чат"), empty);
    emptyTitle->setAlignment(Qt::AlignCenter);
    emptyTitle->setStyleSheet(QStringLiteral("color:#F3F1F8;font-size:24px;font-weight:600;"));
    el->addWidget(emptyTitle);
    auto* emptyHint = new QLabel(
        QStringLiteral("Выберите чат из списка слева, чтобы начать общение"), empty);
    emptyHint->setObjectName(QStringLiteral("emptyHint"));
    emptyHint->setAlignment(Qt::AlignCenter);
    emptyHint->setStyleSheet(QStringLiteral("color:#ACA6BD;font-size:15px;"));
    el->addWidget(emptyHint);
    el->addStretch();

    // Диалог
    auto* conv = new QWidget(convStack_);
    auto* cvl = new QVBoxLayout(conv);
    cvl->setContentsMargins(0, 0, 0, 0);
    cvl->setSpacing(0);

    auto* convHeader = new QWidget(conv);
    convHeader->setObjectName(QStringLiteral("convHeader"));
    convHeader->setFixedHeight(60);
    auto* chl = new QHBoxLayout(convHeader);
    chl->setContentsMargins(8, 0, 16, 0);
    chl->setSpacing(0);

    // Кнопка «назад к темам» (видна только внутри темы форума).
    topicBackBtn_ = new QPushButton(convHeader);
    topicBackBtn_->setObjectName(QStringLiteral("hdrBtn"));
    topicBackBtn_->setCursor(Qt::PointingHandCursor);
    topicBackBtn_->setIcon(Icons::icon(Icons::ArrowLeft, 20, QColor(0xAC,0xA6,0xBD)));
    topicBackBtn_->setIconSize(QSize(20, 20));
    topicBackBtn_->setToolTip(QStringLiteral("К темам"));
    topicBackBtn_->setVisible(false);
    connect(topicBackBtn_, &QPushButton::clicked, this, [this]() {
        currentTopicId_.clear();
        api_->getGroupTopics(currentPeerId_);   // вернуться к списку тем
    });
    chl->addWidget(topicBackBtn_);

    // Кликабельный кластер (аватар + имя/статус) → открывает профиль (как в TG).
    peerHeader_ = new QWidget(convHeader);
    peerHeader_->setObjectName(QStringLiteral("peerHeader"));
    peerHeader_->setCursor(Qt::PointingHandCursor);
    peerHeader_->installEventFilter(this);
    auto* phl = new QHBoxLayout(peerHeader_);
    phl->setContentsMargins(8, 0, 8, 0);
    phl->setSpacing(12);
    peerAvatar_ = makeAvatar(QString(), QStringLiteral("?"), 40);
    peerAvatar_->setParent(peerHeader_);
    auto* names = new QVBoxLayout();
    names->setSpacing(0);
    peerName_ = new QLabel(peerHeader_);
    peerName_->setObjectName(QStringLiteral("peerName"));
    peerStatus_ = new QLabel(peerHeader_);
    peerStatus_->setObjectName(QStringLiteral("peerStatus"));
    names->addWidget(peerName_);
    names->addWidget(peerStatus_);
    phl->addWidget(peerAvatar_);
    phl->addLayout(names);
    chl->addWidget(peerHeader_);
    chl->addStretch();

    // Действия в шапке (как в Telegram): супер-поиск, поиск, звонок, «ещё».
    const QColor hdrIcon(0xAC, 0xA6, 0xBD);
    auto* superBtn = new QPushButton(convHeader);
    superBtn->setObjectName(QStringLiteral("hdrBtn"));
    superBtn->setCursor(Qt::PointingHandCursor);
    superBtn->setIcon(Icons::icon(Icons::Search, 20, QColor(0x8B, 0x5C, 0xF6)));
    superBtn->setIconSize(QSize(20, 20));
    superBtn->setToolTip(QStringLiteral("Супер-поиск (Ctrl+Shift+F)"));
    auto* searchBtn = new QPushButton(convHeader);
    searchBtn->setObjectName(QStringLiteral("hdrBtn"));
    searchBtn->setCursor(Qt::PointingHandCursor);
    searchBtn->setIcon(Icons::icon(Icons::Search, 20, hdrIcon));
    searchBtn->setIconSize(QSize(20, 20));
    searchBtn->setToolTip(QStringLiteral("Поиск сообщений"));
    auto* callBtn = new QPushButton(convHeader);
    callBtn->setObjectName(QStringLiteral("hdrBtn"));
    callBtn->setCursor(Qt::PointingHandCursor);
    callBtn->setIcon(Icons::icon(Icons::Phone, 20, hdrIcon));
    callBtn->setIconSize(QSize(20, 20));
    callBtn->setToolTip(QStringLiteral("Позвонить"));
    moreBtn_ = new QPushButton(convHeader);
    moreBtn_->setObjectName(QStringLiteral("hdrBtn"));
    moreBtn_->setCursor(Qt::PointingHandCursor);
    moreBtn_->setIcon(Icons::icon(Icons::More, 20, hdrIcon));
    moreBtn_->setIconSize(QSize(20, 20));
    chl->addWidget(superBtn);
    chl->addWidget(searchBtn);
    chl->addWidget(callBtn);
    chl->addWidget(moreBtn_);
    connect(superBtn, &QPushButton::clicked, this, &ChatPage::openSuperSearch);
    // Единый поиск (обычный + супер, в этом чате / во всех чатах) — как в вебе.
    connect(searchBtn, &QPushButton::clicked, this, &ChatPage::openSuperSearch);
    connect(callBtn, &QPushButton::clicked, this, &ChatPage::startCall);
    connect(moreBtn_, &QPushButton::clicked, this, &ChatPage::showChatMenu);

    msgScroll_ = new QScrollArea(conv);
    msgScroll_->setObjectName(QStringLiteral("msgArea"));
    msgScroll_->setWidgetResizable(true);
    msgScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Вертикальный скроллбар ВСЕГДА резервирует ширину: иначе на границе
    // «высота контента ≈ высота вьюпорта» он появляется/прячется в цикле,
    // ширина перепосчитывается, бабблы плывут, поток встаёт (GNOME: «не отвечает»).
    msgScroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    msgContainer_ = new QWidget(msgScroll_);
    msgContainer_->setObjectName(QStringLiteral("msgContainer"));
    msgContainer_->setStyleSheet(QStringLiteral("#msgContainer{background:#0B0A0E;}"));
    msgLayout_ = new QVBoxLayout(msgContainer_);
    msgLayout_->setContentsMargins(18, 14, 18, 14);
    msgLayout_->setSpacing(6);
    msgLayout_->addStretch();   // прижимаем сообщения к низу
    msgScroll_->setWidget(msgContainer_);

    // Якорь низа. Раскладка (wordwrap, картинки, новый чат после очистки)
    // успокаивается за несколько проходов, каждый меняет диапазон скролла —
    // держим вид снизу на каждом изменении, пока пользователь не ушёл вверх.
    // Собственный пересчёт всегда ставит value == maximum, поэтому сам себя
    // якорь не отпускает; кламп при сжатии диапазона тоже даёт value == maximum.
    auto* msgSb = msgScroll_->verticalScrollBar();
    connect(msgSb, &QAbstractSlider::rangeChanged, this, [this, msgSb](int, int max) {
        if (stickBottom_) msgSb->setValue(max);
    });
    connect(msgSb, &QAbstractSlider::valueChanged, this, [this, msgSb](int v) {
        if (msgSb->maximum() - v > 24) stickBottom_ = false;   // пользователь ушёл от низа
        else if (!stickBottom_)        stickBottom_ = true;    // вернулся к низу — следим снова
        // Догрузка старых сообщений при прокрутке к верху (Telegram-style).
        // Порог 400px: батч строится чуть ЗАРАНЕЕ, чтобы верх не «упирался»
        // в пустоту (та самая пауза 3–4 секунды перед прогрузкой).
        // Служебные прокрутки (якорь-компенсация, «к низу») не триггерят —
        // иначе цепочка prepend'ов утащит всю историю за один скролл.
        if (!programmaticScroll_ && v < 400) prependOlderMessages();
    });

    // Приветствие пустого чата — оверлей поверх области сообщений.
    greeting_ = new EmptyChatGreeting(msgScroll_);
    greeting_->hide();

    // Кнопка «вниз» (как в Telegram): плавает справа, видна при прокрутке вверх.
    scrollDownBtn_ = new QPushButton(msgScroll_);
    scrollDownBtn_->setObjectName(QStringLiteral("scrollDownBtn"));
    scrollDownBtn_->setCursor(Qt::PointingHandCursor);
    scrollDownBtn_->setFixedSize(44, 44);
    scrollDownBtn_->setText(QStringLiteral("▼"));
    scrollDownBtn_->setIconSize(QSize(22, 22));
    scrollDownBtn_->hide();
    connect(scrollDownBtn_, &QPushButton::clicked, this, [this]() { smoothScrollTo(msgScroll_->verticalScrollBar()->maximum()); });
    scrollAnim_ = new QVariantAnimation(this);
    scrollAnim_->setDuration(320);
    scrollAnim_->setEasingCurve(QEasingCurve::OutCubic);
    connect(scrollAnim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        msgScroll_->verticalScrollBar()->setValue(v.toInt());
    });
    msgScroll_->viewport()->installEventFilter(this);
    connect(greeting_, &EmptyChatGreeting::greetingClicked, this, [this](const QString& e) {
        if (currentPeerId_.isEmpty()) return;
        composer_->setPlainText(e);
        onSendClicked();
    });

    auto* composerBar = new QWidget(conv);
    composerBar->setObjectName(QStringLiteral("composerBar"));
    composerBar_ = composerBar;   // эмодзи-панель привязывается к его правому краю
    auto* cblOuter = new QVBoxLayout(composerBar);
    cblOuter->setContentsMargins(12, 8, 12, 8);   // как .chat-input-area веба

    // Reply-клавиатура бота (как в Telegram): кнопки над полем ввода.
    botKeyboardBar_ = new QWidget(composerBar);
    botKeyboardBar_->setObjectName(QStringLiteral("botKeyboardBar"));
    botKeyboardLayout_ = new QVBoxLayout(botKeyboardBar_);
    botKeyboardLayout_->setContentsMargins(0, 0, 0, 8);
    botKeyboardLayout_->setSpacing(4);
    botKeyboardBar_->setVisible(false);
    cblOuter->addWidget(botKeyboardBar_);

    // Превью ссылки при вводе URL (как composerLinkPreview в вебе).
    linkPreview_ = new LinkPreviewBar(api_, composerBar);
    cblOuter->addWidget(linkPreview_);

    // Полоса «ответ на …» над вводом.
    replyBar_ = new QWidget(composerBar);
    replyBar_->setObjectName(QStringLiteral("replyBar"));
    auto* rbl = new QHBoxLayout(replyBar_);
    rbl->setContentsMargins(16, 6, 12, 6);
    rbl->setSpacing(10);
    auto* rbIcon = new QLabel(replyBar_);
    rbIcon->setPixmap(Icons::pixmap(Icons::Pencil, 16, QColor(0x8B, 0x5C, 0xF6)));
    replyBarText_ = new QLabel(replyBar_);
    replyBarText_->setObjectName(QStringLiteral("replyBarText"));
    auto* rbClose = new QPushButton(QStringLiteral("✕"), replyBar_);
    rbClose->setObjectName(QStringLiteral("replyClose"));
    rbClose->setCursor(Qt::PointingHandCursor);
    rbClose->setFixedSize(24, 24);
    connect(rbClose, &QPushButton::clicked, this, &ChatPage::clearReplyTo);
    rbl->addWidget(rbIcon);
    rbl->addWidget(replyBarText_, 1);
    rbl->addWidget(rbClose);
    replyBar_->setVisible(false);
    cblOuter->addWidget(replyBar_);

    // Полоса отложенных аттачей (MLT-06): дроп файлов → чипы-превью здесь,
    // отправка — вместе со следующим «Отправить» (как pendingAttachments веба).
    stagedBar_ = new QWidget(composerBar);
    stagedBar_->setObjectName(QStringLiteral("stagedBar"));
    stagedLay_ = new QVBoxLayout(stagedBar_);
    stagedLay_->setContentsMargins(8, 4, 8, 8);
    stagedLay_->setSpacing(6);
    stagedBar_->setVisible(false);
    cblOuter->addWidget(stagedBar_);

    composerStack_ = new QStackedWidget(composerBar);
    cblOuter->addWidget(composerStack_);

    // Страница 0 — обычный ввод, всё внутри одной пилюли (как .tg-input-bar):
    // [⏱][📎][текст][😀][🎤/➤]
    auto* normal = new QWidget();
    normal->setObjectName(QStringLiteral("tgInputBar"));
    auto* cbl = new QHBoxLayout(normal);
    cbl->setContentsMargins(8, 4, 4, 4);
    cbl->setSpacing(2);

    const QColor iconClr(0xAC, 0xA6, 0xBD);
    timerBtn_ = new QPushButton(normal);
    timerBtn_->setObjectName(QStringLiteral("composerIcon"));
    timerBtn_->setCursor(Qt::PointingHandCursor);
    timerBtn_->setToolTip(QStringLiteral("Исчезающие сообщения"));
    timerBtn_->setIcon(Icons::icon(Icons::Clock, 20, iconClr));
    timerBtn_->setIconSize(QSize(20, 20));

    attachBtn_ = new QPushButton(normal);
    attachBtn_->setObjectName(QStringLiteral("composerIcon"));
    attachBtn_->setCursor(Qt::PointingHandCursor);
    attachBtn_->setToolTip(QStringLiteral("Прикрепить"));
    attachBtn_->setIcon(Icons::icon(Icons::Paperclip, 20, iconClr));
    attachBtn_->setIconSize(QSize(20, 20));

    composer_ = new ComposerEdit(normal);
    composer_->installEventFilter(this);   // Esc → закрыть панель эмодзи
    composer_->setObjectName(QStringLiteral("composer"));
    composer_->setPlaceholderText(QStringLiteral("Сообщение…"));
    connect(composer_, &ComposerEdit::sendRequested, this, &ChatPage::onSendClicked);
    connect(composer_, &ComposerEdit::imagePasted, this, [this](const QImage& img) {
        if (currentPeerId_.isEmpty() || img.isNull()) return;
        QByteArray bytes;
        QBuffer buf(&bytes); buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
        sendPhotoBytes(bytes, QStringLiteral("pasted.png"));
    });

    emojiBtn_ = new QPushButton(normal);
    emojiBtn_->setObjectName(QStringLiteral("composerIcon"));
    emojiBtn_->setCursor(Qt::PointingHandCursor);
    emojiBtn_->setToolTip(QStringLiteral("Эмодзи"));
    emojiBtn_->setIcon(Icons::icon(Icons::Smile, 20, iconClr));
    emojiBtn_->setIconSize(QSize(20, 20));

    micBtn_ = new QPushButton(normal);
    micBtn_->setObjectName(QStringLiteral("micBtn"));
    micBtn_->setCursor(Qt::PointingHandCursor);
    micBtn_->setIcon(Icons::icon(Icons::Mic, 20, iconClr));
    micBtn_->setIconSize(QSize(20, 20));
    sendBtn_ = new QPushButton(normal);
    sendBtn_->setObjectName(QStringLiteral("sendBtn"));
    sendBtn_->setCursor(Qt::PointingHandCursor);
    sendBtn_->setIcon(Icons::icon(Icons::Send, 20, QColor(0x8B, 0x5C, 0xF6)));
    sendBtn_->setIconSize(QSize(20, 20));
    sendBtn_->setVisible(false);   // как в вебе: ➤ появляется при вводе текста, 🎤 уходит

    cbl->addWidget(attachBtn_);
    cbl->addWidget(timerBtn_);
    cbl->addWidget(composer_, 1);
    cbl->addWidget(emojiBtn_);
    cbl->addWidget(micBtn_);
    cbl->addWidget(sendBtn_);

    // Тоггл «🎤 ↔ ➤» по наличию текста — 1:1 с поведением композера веба.
    connect(composer_, &ComposerEdit::textChanged, this, [this]() {
        const bool hasText = !composer_->toPlainText().isEmpty();
        sendBtn_->setVisible(hasText);
        micBtn_->setVisible(!hasText);
        linkPreview_->updateForText(composer_->toPlainText());   // превью ссылки
    });

    // Страница 1 — запись (стиль Discord): пульс + waveform + таймер.
    recBar_ = new RecordingBar();

    composerStack_->addWidget(normal);    // 0
    composerStack_->addWidget(recBar_);   // 1
    composerStack_->setCurrentIndex(0);

    cvl->addWidget(convHeader);

    cvl->addWidget(msgScroll_, 1);
    cvl->addWidget(composerBar);

    // Глобальная панель загрузок (как в Telegram): переживает смену чата.
    auto* dlBar = new DownloadBar(conv);
    cvl->addWidget(dlBar);
    connect(dlBar, &DownloadBar::cancelRequested, this, [this](const QString& p) {
        api_->cancelFetch(p);
        DownloadCenter::instance().cancel(p);
    });

    // ── Страница списка тем форума (index 2) ────────────────────────────────────
    topicsPage_ = new QWidget(convStack_);
    auto* topicsPage = topicsPage_;
    topicsPage->setStyleSheet(QStringLiteral("background:#0B0A0E;"));
    auto* tpl = new QVBoxLayout(topicsPage);
    tpl->setContentsMargins(0, 0, 0, 0);
    tpl->setSpacing(0);
    auto* tHead = new QWidget(topicsPage);
    tHead->setObjectName(QStringLiteral("convHeader"));
    tHead->setFixedHeight(60);
    auto* thl = new QHBoxLayout(tHead);
    thl->setContentsMargins(16, 0, 12, 0);
    topicsTitle_ = new QLabel(QStringLiteral("Темы"), tHead);
    topicsTitle_->setObjectName(QStringLiteral("peerName"));
    topicsTitle_->setCursor(Qt::PointingHandCursor);
    topicsTitle_->installEventFilter(this);
    topicsTitle_->setProperty("openPeerInfo", true);
    auto* newTopicBtn = new QPushButton(tHead);
    newTopicBtn->setObjectName(QStringLiteral("hdrBtn"));
    newTopicBtn->setCursor(Qt::PointingHandCursor);
    newTopicBtn->setIcon(Icons::icon(Icons::Plus, 20, QColor(0xAC,0xA6,0xBD)));
    newTopicBtn->setIconSize(QSize(20, 20));
    newTopicBtn->setToolTip(QStringLiteral("Новая тема"));
    connect(newTopicBtn, &QPushButton::clicked, this, &ChatPage::createTopicDialog);
    thl->addWidget(topicsTitle_);
    thl->addStretch();
    thl->addWidget(newTopicBtn);
    tpl->addWidget(tHead);
    auto* tScroll = new QScrollArea(topicsPage);
    tScroll->setObjectName(QStringLiteral("chatList"));
    tScroll->setWidgetResizable(true);
    tScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tScroll->setFrameShape(QFrame::NoFrame);
    auto* tListW = new QWidget();
    topicsBox_ = new QVBoxLayout(tListW);
    topicsBox_->setContentsMargins(8, 8, 8, 8);
    topicsBox_->setSpacing(4);
    topicsBox_->addStretch();
    tScroll->setWidget(tListW);
    tpl->addWidget(tScroll, 1);

    convStack_->addWidget(empty);       // 0
    convStack_->addWidget(conv);        // 1
    convStack_->addWidget(topicsPage);  // 2
    convStack_->setCurrentIndex(0);

    // ── Третья колонка (WIN-01): контейнер инфо о чате справа ─────────────────
    // В сплиттере: [сайдбар 380 | чат | инфо 0/360/фулл]; в узком окне (<1000px)
    // уходит в оверлей поверх чата со скримом (WIN-02: анимация 200мс).
    thirdCol_ = new QFrame();
    thirdCol_->setObjectName(QStringLiteral("thirdCol"));
    thirdCol_->setMinimumWidth(0);
    thirdCol_->setMaximumWidth(0);   // исходно закрыта
    {
        auto* tv = new QVBoxLayout(thirdCol_);
        tv->setContentsMargins(0, 0, 0, 0);
        tv->setSpacing(0);
        auto* tcHead = new QWidget(thirdCol_);
        tcHead->setObjectName(QStringLiteral("thirdColHeader"));
        tcHead->setAttribute(Qt::WA_StyledBackground, true);
        tcHead->setFixedHeight(60);
        auto* thl = new QHBoxLayout(tcHead);
        thl->setContentsMargins(16, 0, 8, 0);
        thl->setSpacing(4);
        auto* tcTitle = new QLabel(QStringLiteral("Информация"), tcHead);
        tcTitle->setObjectName(QStringLiteral("tcTitle"));
        thl->addWidget(tcTitle);
        thl->addStretch();
        tcExpandBtn_ = new QPushButton(QStringLiteral("⤢"), tcHead);
        tcExpandBtn_->setObjectName(QStringLiteral("tcBtn"));
        tcExpandBtn_->setCursor(Qt::PointingHandCursor);
        tcExpandBtn_->setToolTip(QStringLiteral("Развернуть/свернуть"));
        tcExpandBtn_->setFixedSize(36, 36);
        connect(tcExpandBtn_, &QPushButton::clicked, this, [this]() {
            // 360 ↔ фулл (45% окна); Esc/✕ закрывает совсем.
            setThirdColumnOpen(true, !thirdColFull_);
        });
        thl->addWidget(tcExpandBtn_);
        auto* tcClose = new QPushButton(QStringLiteral("✕"), tcHead);
        tcClose->setObjectName(QStringLiteral("tcBtn"));
        tcClose->setCursor(Qt::PointingHandCursor);
        tcClose->setFixedSize(36, 36);
        connect(tcClose, &QPushButton::clicked, this, [this]() { closeThirdColumn(); });
        thl->addWidget(tcClose);
        tv->addWidget(tcHead);

        auto* tcScroll = new QScrollArea(thirdCol_);
        tcScroll->setObjectName(QStringLiteral("tcScroll"));
        tcScroll->setWidgetResizable(true);
        tcScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        tcScroll->setFrameShape(QFrame::NoFrame);
        auto* tcBody = new QWidget();
        auto* bv = new QVBoxLayout(tcBody);
        bv->setContentsMargins(20, 20, 20, 20);
        bv->setSpacing(8);
        tcAvatar_ = new QLabel(tcBody);
        tcAvatar_->setAlignment(Qt::AlignHCenter);
        bv->addWidget(tcAvatar_, 0, Qt::AlignHCenter);
        tcName_ = new QLabel(tcBody);
        tcName_->setObjectName(QStringLiteral("tcName"));
        tcName_->setAlignment(Qt::AlignHCenter);
        tcName_->setWordWrap(true);
        bv->addWidget(tcName_);
        tcSub_ = new QLabel(tcBody);
        tcSub_->setObjectName(QStringLiteral("tcSub"));
        tcSub_->setAlignment(Qt::AlignHCenter);
        tcSub_->setWordWrap(true);
        bv->addWidget(tcSub_);
        bv->addSpacing(8);
        auto* tcProfile = new QPushButton(QStringLiteral("Полный профиль"), tcBody);
        tcProfile->setObjectName(QStringLiteral("tcAction"));
        connect(tcProfile, &QPushButton::clicked, this, [this]() {
            // Полная карточка — прежняя модалка профиля (клик по шапке веба).
            if (currentPeerId_.isEmpty()) return;
            ensureProfilePanel();
            const int pi = indexOfChat(currentPeerId_);
            if (pi >= 0) profilePanel_->setKnownPreview(
                chats_[pi].displayName, chats_[pi].avatarUrl, chats_[pi].online);
            profilePanel_->openFor(currentPeerId_);
        });
        bv->addWidget(tcProfile);
        auto* tcCall = new QPushButton(QStringLiteral("Позвонить"), tcBody);
        tcCall->setObjectName(QStringLiteral("tcActionGhost"));
        connect(tcCall, &QPushButton::clicked, this, [this]() {
            if (currentPeerId_.isEmpty()) return;
            emit callRequested(currentPeerId_, currentPeerName_, QString());
        });
        bv->addWidget(tcCall);
        // Управление группой/каналом — прежняя панель PeerInfoPanel (модалкой).
        auto* tcManage = new QPushButton(QStringLiteral("Управление"), tcBody);
        tcManage->setObjectName(QStringLiteral("tcActionGhost"));
        tcManage->setVisible(false);
        connect(tcManage, &QPushButton::clicked, this, [this]() {
            if (currentPeerId_.isEmpty()
                || (currentKind_ != ChatKind::Group && currentKind_ != ChatKind::Channel))
                return;
            const int idx = indexOfChat(currentPeerId_);
            const QString av = idx >= 0 ? chats_[idx].avatarUrl : QString();
            auto* info = new PeerInfoPanel(api_, currentPeerId_,
                                           currentKind_ == ChatKind::Channel,
                                           currentPeerName_, av, window());
            connect(info, &PeerInfoPanel::changed, this,
                    [this]() { api_->getGroups(); api_->getChannels(); });
            connect(info, &PeerInfoPanel::leftPeer, this, [this](const QString&) {
                api_->getGroups(); api_->getChannels();
                convStack_->setCurrentIndex(0);
                currentPeerId_.clear();
            });
            info->showAnimated();
        });
        bv->addWidget(tcManage);
        tcManageBtn_ = tcManage;
        bv->addSpacing(4);
        tcMeta_ = new QWidget(tcBody);
        auto* mv = new QVBoxLayout(tcMeta_);
        mv->setContentsMargins(0, 8, 0, 0);
        mv->setSpacing(8);
        bv->addWidget(tcMeta_);
        bv->addStretch();
        tcScroll->setWidget(tcBody);
        tv->addWidget(tcScroll, 1);
    }

    // Скрим оверлея: затемнение ЧАТА (не сайдбара) в узком режиме; клик — закрыть.
    overlayScrim_ = new QWidget(this);
    overlayScrim_->setObjectName(QStringLiteral("overlayScrim"));
    overlayScrim_->hide();
    scrimFx_ = new QGraphicsOpacityEffect(overlayScrim_);
    scrimFx_->setOpacity(0.0);
    overlayScrim_->setGraphicsEffect(scrimFx_);
    overlayScrim_->installEventFilter(this);

    // Три колонки (WIN-01): сайдбар | чат | инфо, перетаскиваемая граница
    // между чатом и инфо (ручка сплиттера), сайдбар фиксирован как в вебе.
    splitter_ = new QSplitter(Qt::Horizontal, this);
    splitter_->setObjectName(QStringLiteral("mainSplitter"));
    splitter_->setChildrenCollapsible(false);
    splitter_->setHandleWidth(4);
    splitter_->addWidget(sidebar);
    splitter_->addWidget(convStack_);
    splitter_->addWidget(thirdCol_);
    splitter_->setStretchFactor(0, 0);
    splitter_->setStretchFactor(1, 1);
    splitter_->setStretchFactor(2, 0);
    splitter_->setSizes({380, width() - 380, 0});

    root->addWidget(splitter_);

    connect(newChatBtn, &QPushButton::clicked, this, &ChatPage::openNewChatDialog);
    connect(menuBtn_, &QPushButton::clicked, this, &ChatPage::toggleAppMenu);
    connect(chatList_, &QListWidget::itemClicked, this, &ChatPage::onChatClicked);
    chatList_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(chatList_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& p) {
        QListWidgetItem* it = chatList_->itemAt(p);
        if (!it) return;
        const QString id = it->data(Qt::UserRole).toString();
        const int idx = indexOfChat(id);
        if (idx < 0) return;
        const Chat c = chats_[idx];
        QMenu menu(this);
        QAction* open = menu.addAction(QStringLiteral("Открыть"));
        // Пин чата (LST-04): закреплённые держатся секцией сверху списка.
        const bool pinned = pinnedChats_.contains(chatKeyFor(c));
        QAction* pin = menu.addAction(pinned ? QStringLiteral("📌 Открепить сверху")
                                             : QStringLiteral("📌 Закрепить сверху"));
        // Без звука: локальный мьют (как в вебе, ключ xipher_muted_chats).
        const bool muted = Prefs::getStr(QStringLiteral("xipher_muted_chats"))
                               .contains(QStringLiteral("chat:") + c.id
                                         + QStringLiteral(","));
        QAction* mute = menu.addAction(muted ? QStringLiteral("🔔 Со звуком")
                                             : QStringLiteral("🔇 Без звука"));
        // Архив: чат уезжает в секцию «Архив» (локальные ключи в Prefs, LST-03).
        const bool archived = archivedChats_.contains(chatKeyFor(c));
        QAction* arch = menu.addAction(archived ? QStringLiteral("📤 Вернуть из архива")
                                                : QStringLiteral("📥 В архив"));
        QAction* clear = menu.addAction(QStringLiteral("🧹 Очистить переписку"));
        QAction* del = nullptr;
        if (c.kind == ChatKind::Group)        del = menu.addAction(QStringLiteral("Выйти из группы"));
        else if (c.kind == ChatKind::Channel) del = menu.addAction(QStringLiteral("Отписаться"));
        else if (!c.isSaved)                  del = menu.addAction(QStringLiteral("Удалить чат"));
        QAction* ch = menu.exec(chatList_->mapToGlobal(p));
        if (ch == open) openChat(c);
        else if (ch == pin) setChatPinned(c, !pinned);
        else if (ch == mute) {
            // Локальная пара add/remove, как в старом кольце-реализации.
            QString cur = Prefs::getStr(QStringLiteral("xipher_muted_chats"));
            const QString key = QStringLiteral("chat:") + c.id + QStringLiteral(",");
            if (muted) cur.replace(key, QString());
            else cur += key;
            Prefs::setStr(QStringLiteral("xipher_muted_chats"), cur);
        }
        else if (ch == arch) {
            const QString akey = chatKeyFor(c);
            if (archived) archivedChats_.remove(akey);
            else          archivedChats_.insert(akey);
            saveArchivedChats();
            rebuildChatList();
        }
        else if (ch == clear) {
            if (QMessageBox::question(this, QStringLiteral("Очистка"),
                    QStringLiteral("Удалить всю переписку в «%1»?").arg(c.displayName))
                == QMessageBox::Yes) {
                api_->clearHistory(c.id, c.kind);
            }
        }
        else if (del && ch == del) {
            if (c.kind == ChatKind::Group)        api_->leaveGroup(c.id);
            else if (c.kind == ChatKind::Channel) api_->unsubscribeChannel(c.id);
            else                                  api_->deleteChat(c.id);
            if (c.id == currentPeerId_) { convStack_->setCurrentIndex(0); currentPeerId_.clear(); }
        }
    });
    connect(sendBtn_, &QPushButton::clicked, this, &ChatPage::onSendClicked);
    connect(search_, &QLineEdit::textChanged, this, &ChatPage::onSearchChanged);
    connect(micBtn_, &QPushButton::clicked, this, &ChatPage::onMicClicked);
    connect(recBar_, &RecordingBar::cancelClicked, this, &ChatPage::cancelRecording);
    connect(recBar_, &RecordingBar::sendClicked, this, &ChatPage::stopAndSendVoice);
    connect(emojiBtn_, &QPushButton::clicked, this, &ChatPage::onEmojiClicked);
    connect(attachBtn_, &QPushButton::clicked, this, &ChatPage::onAttachClicked);
    connect(timerBtn_, &QPushButton::clicked, this, &ChatPage::onTimerClicked);
    connect(api_, &ApiClient::fileUploaded, this, &ChatPage::onFileUploaded);
    connect(api_, &ApiClient::historyCleared, this,
            [this](const QString& chatId, bool ok) {
        if (!ok || chatId != currentPeerId_) return;
        currentMessages_.clear();
        shownIds_.clear();
        renderedFrom_ = 0;
        renderMessages(QString());
        ChatCache::instance().remove(cacheKey());
    });
    connect(api_, &ApiClient::contactRenamed, this,
            [this](const QString& cid, const QString& name, bool ok) {
        if (!ok) return;
        const int idx = indexOfChat(cid);
        if (idx >= 0) { chats_[idx].displayName = name; rebuildChatList(); }
        if (cid == currentPeerId_) { currentPeerName_ = name; peerName_->setText(name); }
    });
    // Пины чатов (LST-04): сервер — источник правды; оптимистичный пин/анпин
    // откатывается при ошибке (как setChatPinned в вебе).
    connect(api_, &ApiClient::chatPinsLoaded, this, [this](const QSet<QString>& keys) {
        pinnedChats_ = keys;
        rebuildChatList();
    });
    connect(api_, &ApiClient::chatPinDone, this,
            [this](const QString& key, bool pinned, bool ok, const QString& error) {
        if (ok) return;
        if (pinned) pinnedChats_.remove(key);
        else        pinnedChats_.insert(key);
        rebuildChatList();
        QString msg = error;
        if (msg == QLatin1String("Pinned chats limit reached"))
            msg = QStringLiteral("Лимит закреплений исчерпан (с Xipher Pulse — 10).");
        if (msg.isEmpty()) msg = QStringLiteral("Не удалось обновить закрепление");
        QMessageBox::warning(this, QStringLiteral("Закрепления"), msg);
    });

    // Подсветка дропа (MLT-06): полупрозрачный слой поверх всего экрана чата.
    dropOverlay_ = new QWidget(this);
    dropOverlay_->setObjectName(QStringLiteral("dropOverlay"));
    dropOverlay_->hide();

    // Quick Switcher (DSC-01): Ctrl+K — мгновенный переход между чатами.
    auto* qsHotkey = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), this);
    connect(qsHotkey, &QShortcut::activated, this, [this]() { openQuickSwitcher(); });

    // Клавиатурная навигация (KEY-02..04): Ctrl+PgUp/PgDn — соседний чат,
    // Alt+←/→ — история переходов, Ctrl+↑ — правка последнего своего.
    auto* nextChat = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_PageDown), this);
    connect(nextChat, &QShortcut::activated, this, [this]() { cycleChat(+1); });
    auto* prevChat = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_PageUp), this);
    connect(prevChat, &QShortcut::activated, this, [this]() { cycleChat(-1); });
    auto* navBack = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Left), this);
    connect(navBack, &QShortcut::activated, this, [this]() { navigateChatHistory(-1); });
    auto* navFwd = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Right), this);
    connect(navFwd, &QShortcut::activated, this, [this]() { navigateChatHistory(+1); });
    auto* editLast = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Up), this);
    connect(editLast, &QShortcut::activated, this, &ChatPage::editLastOwnMessage);

    // Тема из настроек («Оформление»): перегенерировать QSS и инлайн-фоны.
    applyTheme();
}

// ── Quick Switcher (DSC-01) ──────────────────────────────────────────────────

void ChatPage::openQuickSwitcher() {
    if (!quickSwitcher_) {
        quickSwitcher_ = new QuickSwitcher(chats_, window());
        connect(quickSwitcher_, &QuickSwitcher::picked, this, [this](const Chat& c) {
            openChat(c);
        });
        // ModalOverlay самоудаляется после закрытия — сбрасываем кэш.
        connect(quickSwitcher_, &ModalOverlay::closed, this,
                [this]() { quickSwitcher_ = nullptr; });
    }
    quickSwitcher_->setChats(chats_);   // список мог обновиться, пока был закрыт
    quickSwitcher_->resetForOpen();
    quickSwitcher_->showAnimated();
}

// ── Третья колонка (WIN-01/WIN-02) ───────────────────────────────────────────

void ChatPage::setThirdColumnInfo() {
    if (!thirdCol_) return;
    if (currentPeerId_.isEmpty()) {
        tcName_->setText(QStringLiteral("Чат не выбран"));
        tcSub_->clear();
        Avatar::setRound(tcAvatar_, QString(), QStringLiteral("?"), 96);
        return;
    }
    const int pi = indexOfChat(currentPeerId_);
    const QString av = pi >= 0 ? chats_[pi].avatarUrl : QString();
    Avatar::setRound(tcAvatar_, currentKind_ == ChatKind::User ? av : QString(),
                     currentPeerName_.mid(0, 1), 96);
    tcName_->setText(currentPeerName_);
    tcSub_->setText(peerStatus_->text());
    if (tcManageBtn_)
        tcManageBtn_->setVisible(currentKind_ == ChatKind::Group
                                 || currentKind_ == ChatKind::Channel);

    // Мета-строки: тип, участники/подписчики, @ссылка — по данным чата.
    if (QLayout* ml = tcMeta_->layout()) {
        while (ml->count() > 0) {
            QLayoutItem* it = ml->takeAt(0);
            if (it->widget()) it->widget()->deleteLater();
            delete it;
        }
        auto addRow = [this, ml](const QString& k, const QString& v) {
            if (v.isEmpty()) return;
            auto* row = new QWidget(tcMeta_);
            auto* rl = new QHBoxLayout(row);
            rl->setContentsMargins(0, 0, 0, 0);
            rl->setSpacing(8);
            auto* kk = new QLabel(k, row);
            kk->setObjectName(QStringLiteral("tcMetaKey"));
            auto* vv = new QLabel(v, row);
            vv->setObjectName(QStringLiteral("tcMetaVal"));
            vv->setWordWrap(true);
            rl->addWidget(kk);
            rl->addStretch();
            rl->addWidget(vv, 1);
            ml->addWidget(row);
        };
        const Chat c = pi >= 0 ? chats_[pi] : Chat();
        addRow(QStringLiteral("Тип"),
               currentKind_ == ChatKind::Group ? QStringLiteral("Группа")
               : currentKind_ == ChatKind::Channel ? QStringLiteral("Канал")
               : QString());
        if (c.membersCount > 0)
            addRow(currentKind_ == ChatKind::Channel ? QStringLiteral("Подписчики")
                                                     : QStringLiteral("Участники"),
                   QString::number(c.membersCount));
        if (!c.customLink.isEmpty())
            addRow(QStringLiteral("Ссылка"), QStringLiteral("@") + c.customLink);
        if (!c.name.isEmpty())
            addRow(QStringLiteral("Имя пользователя"), QStringLiteral("@") + c.name);
    }
}

void ChatPage::animateThirdColumnTo(int targetW) {
    const int from = thirdCol_->width();
    if (!thirdColAnim_) {
        thirdColAnim_ = new QVariantAnimation(this);
        thirdColAnim_->setDuration(200);   // WIN-02: 200мс, без миганий
        thirdColAnim_->setEasingCurve(QEasingCurve::OutCubic);
        connect(thirdColAnim_, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& v) {
            const int w = v.toInt();
            if (thirdColOverlayMode_) {
                thirdCol_->setMaximumWidth(QWIDGETSIZE_MAX);
                thirdCol_->setGeometry(width() - w, 0, w, height());
                thirdCol_->raise();
            } else {
                thirdCol_->setMinimumWidth(w);
                thirdCol_->setMaximumWidth(w);
            }
        });
        connect(thirdColAnim_, &QVariantAnimation::finished, this, [this]() {
            if (thirdColTarget_ == 0) {
                // Закрыто: панель возвращается в сплиттер нулевой ширины
                // (из оверлей-режима — с погашенным скримом).
                if (thirdCol_->parentWidget() != splitter_) {
                    fadeScrim(false);
                    thirdCol_->setParent(splitter_);
                    splitter_->addWidget(thirdCol_);
                    splitter_->setStretchFactor(2, 0);
                }
                thirdCol_->setMinimumWidth(0);
                thirdCol_->setMaximumWidth(0);
                thirdCol_->hide();
            } else if (!thirdColOverlayMode_) {
                // Открыто в сплиттере: границы отпускаем — ручку можно тащить
                // (во время анимации min=max=w держали ширину жёстко).
                thirdCol_->setMinimumWidth(280);
                thirdCol_->setMaximumWidth(qMax(360, width() / 2));
            }
            updateThirdColumnMode();   // режим мог поменяться за 200мс
        });
    }
    if (thirdColAnim_->state() == QAbstractAnimation::Running) thirdColAnim_->stop();
    thirdColAnim_->setStartValue(from);
    thirdColAnim_->setEndValue(targetW);
    thirdColAnim_->start();   // персистентная, живёт пока жива страница
}

void ChatPage::setThirdColumnOpen(bool open, bool full) {
    if (!thirdCol_) return;
    thirdColFull_ = open && full;
    tcExpandBtn_->setText(thirdColFull_ ? QStringLiteral("⤡") : QStringLiteral("⤢"));

    // Целевая ширина: 0 (закрыто) / 360 / 45% окна в «фулл».
    int target = 0;
    if (open) {
        target = full ? qBound(360, width() * 45 / 100, width() - 420) : 360;
        if (target < 360) target = 360;
    }
    thirdColTarget_ = target;
    if (target > 0) {
        setThirdColumnInfo();
        thirdCol_->show();
        // Оверлей-режим по ширине окна (<1000px): панель поверх чата + скрим.
        updateThirdColumnMode();
    }
    animateThirdColumnTo(target);
}

void ChatPage::toggleThirdColumn() {
    setThirdColumnOpen(!(thirdColTarget_ > 0), false);
}

void ChatPage::closeThirdColumn() {
    setThirdColumnOpen(false);
}

void ChatPage::fadeScrim(bool on) {
    if (!scrimFx_) return;
    auto* fade = new QPropertyAnimation(scrimFx_, "opacity", this);
    fade->setDuration(200);
    fade->setStartValue(scrimFx_->opacity());
    fade->setEndValue(on ? 0.55 : 0.0);
    if (on) overlayScrim_->show();
    connect(fade, &QPropertyAnimation::finished, this, [this, on]() {
        if (!on) overlayScrim_->hide();
    });
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

void ChatPage::updateThirdColumnMode() {
    if (!thirdCol_ || thirdColTarget_ <= 0) return;
    const bool wantOverlay = width() < 1000;
    if (wantOverlay == thirdColOverlayMode_) {
        // Режим тот же — только геометрия оверлея плывёт с окном.
        if (thirdColOverlayMode_) {
            const int w = qMin(thirdColTarget_,
                               qMax(280, width() - sidebarWidth() - 24));
            overlayScrim_->setGeometry(sidebarWidth(), 0,
                                       width() - sidebarWidth(), height());
            overlayScrim_->raise();
            thirdCol_->setGeometry(width() - w, 0, w, height());
            thirdCol_->raise();
        }
        return;
    }
    thirdColOverlayMode_ = wantOverlay;
    if (wantOverlay) {
        // Из сплиттера — в свободный оверлей правого края.
        thirdCol_->setParent(this);
        thirdCol_->setMaximumWidth(QWIDGETSIZE_MAX);
        thirdCol_->setMinimumWidth(0);
        thirdCol_->show();   // setParent() скрывает виджет
        const int w = qMin(thirdColTarget_,
                           qMax(280, width() - sidebarWidth() - 24));
        overlayScrim_->setGeometry(sidebarWidth(), 0,
                                   width() - sidebarWidth(), height());
        overlayScrim_->show();
        overlayScrim_->raise();
        thirdCol_->setGeometry(width() - w, 0, w, height());
        thirdCol_->raise();
        fadeScrim(true);
    } else {
        // Обратно в сплиттер (границы отпущены — ручка тащится).
        fadeScrim(false);
        thirdCol_->setParent(splitter_);
        splitter_->addWidget(thirdCol_);
        splitter_->setStretchFactor(2, 0);
        thirdCol_->setMinimumWidth(280);
        thirdCol_->setMaximumWidth(qMax(360, width() / 2));
        const int sbw = sidebarWidth();
        splitter_->setSizes({sbw, qMax(0, width() - sbw - thirdColTarget_), thirdColTarget_});
        thirdCol_->show();
    }
}

int ChatPage::sidebarWidth() const {
    return sidebar_ ? sidebar_->width() : 0;
}

void ChatPage::load() {
    api_->getChats();
    api_->getGroups();
    api_->getChannels();
    api_->getChatFolders();
    api_->getChatPins();   // закреплённые чаты — секция сверху списка (LST-04)
    loadStoriesUi();
    if (!Session::instance().token.isEmpty())
        ws_->start(Session::instance().token);
}

// ── Единый поиск (Ctrl+Shift+F; слияние «обычного» и супер-поиска) ───────────

void ChatPage::openSuperSearch() {
    if (!superSearch_) {
        superSearch_ = new SuperSearchDialog(api_, this);
        superSearch_->setGeometry(rect());
        connect(superSearch_, &SuperSearchDialog::resultPicked,
                this, &ChatPage::onSearchResultPicked);
    }
    superSearch_->setChats(chats_);   // для области «Во всех чатах»
    // Группа (форум-темы считаем группой) или личка; без чата — глобально.
    const QString ctx = (currentKind_ == ChatKind::Group || currentKind_ == ChatKind::Channel)
        ? QStringLiteral("group") : QStringLiteral("dm");
    superSearch_->openFor(currentTopicId_.isEmpty() ? currentPeerId_ : currentTopicId_, ctx);
}

void ChatPage::onSearchResultPicked(const QString& chatId, const QString& messageId,
                                    const QString& keywords) {
    if (chatId.isEmpty()) return;
    // Подсветка запроса в бабблах (SRC-04): перерисовка + прыжок.
    highlightQuery_ = keywords.trimmed();
    if (chatId == currentPeerId_ && currentTopicId_.isEmpty()) {
        if (!highlightQuery_.isEmpty()) rerenderPreservingScroll();
        jumpToMessage(messageId);
        return;
    }
    // Другой чат: открываем, а прыжок выполняем, когда история отрисуется
    // (pendingJumpId_ доедает tryJumpToPending).
    const int idx = indexOfChat(chatId);
    pendingJumpId_ = messageId;
    if (idx >= 0) {
        openChat(chats_[idx]);
    } else {
        openChatWith(chatId, QString(), QString());
    }
}

void ChatPage::tryJumpToPending() {
    if (pendingJumpId_.isEmpty()) return;
    const QString target = pendingJumpId_;
    const auto bubbles = msgContainer_->findChildren<QFrame*>();
    for (QFrame* b : bubbles)
        if (b->property("msgId").toString() == target) {
            pendingJumpId_.clear();
            jumpToMessage(target);
            return;
        }
    // Виджета ещё нет (сообщение глубже хвоста) — догружаем батч из истории
    // или ждём серверную пагинацию; continueTail дергает tryJumpToPending.
    if (renderedFrom_ <= 0 && !hasMoreServer_ && !fetchingOlder_)
        pendingJumpId_.clear();   // история кончилась, сообщения нет — сдаёмся
}

void ChatPage::jumpToMessage(const QString& messageId) {
    // Ищем виджет баббла по msgId и прокручиваем к нему с подсветкой.
    const auto bubbles = msgContainer_->findChildren<QFrame*>();
    QFrame* target = nullptr;
    for (QFrame* b : bubbles)
        if (b->property("msgId").toString() == messageId) { target = b; break; }
    if (!target) return;
    auto* sb = msgScroll_->verticalScrollBar();
    const int y = target->mapTo(msgContainer_, QPoint(0, 0)).y();
    programmaticScroll_ = true;
    sb->setValue(qBound(sb->minimum(), y - msgScroll_->height() / 3, sb->maximum()));
    programmaticScroll_ = false;
    // Короткая подсветка, как ss-highlight-pulse в вебе.
    stickBottom_ = false;   // прыжок в историю — якорь низа отпускаем
    target->setStyleSheet(target->styleSheet()
        + QStringLiteral("QFrame{border:2px solid #8B5CF6;}"));
    QTimer::singleShot(2200, this, [this, target]() {
        if (!target) return;
        const bool sent = target->objectName() == QStringLiteral("bubbleOut");
        target->setStyleSheet(sent ? bubbleOutQss() : bubbleInQss());
    });
}

// ── Сторис (как stories.src.js веба) ─────────────────────────────────────────

void ChatPage::loadStoriesUi() {
    connect(api_, &ApiClient::storiesLoaded, this, [this](const QJsonObject& data) {
        bool premium = false;
        storyGroups_ = groupStoriesPayload(data, Session::instance().userId, &premium);
        storiesBar_->applyData(storyGroups_);
    });
    connect(storiesBar_, &StoriesBar::userClicked, this, [this](int idx) {
        if (!storiesViewer_) storiesViewer_ = new StoriesViewer(api_, window());
        storiesViewer_->open(storyGroups_, idx);
    });
    connect(storiesBar_, &StoriesBar::addRequested, this, [this]() {
        if (!storyCreator_) storyCreator_ = new StoryCreatorDialog(api_, window());
        storyCreator_->show();
    });
    // После публикации — обновить бар (успех приходит сигналом storyCreated).
    connect(api_, &ApiClient::storyCreated, this,
            [this](bool ok, const QString&) { if (ok) api_->loadStories(); },
            static_cast<Qt::ConnectionType>(Qt::UniqueConnection));
    api_->loadStories();
}

void ChatPage::resizeEvent(QResizeEvent* e) {
    // Узкое окно: сайдбар сжимается до 300px (как minmax(300px, 380px) в вебе).
    if (sidebar_) {
        const int target = qBound(300, width() * 30 / 100, 380);
        if (sidebar_->width() != target) sidebar_->setFixedWidth(target);
    }
    // Третья колонка (WIN-01): <1000px — оверлей, иначе — в сплиттере.
    updateThirdColumnMode();
    clampBubbleWidths();
    // Повтор после layout-прохода: контейнер сообщений меняет ширину с отставанием.
    QTimer::singleShot(0, this, &ChatPage::clampBubbleWidths);
    updateScrollDownButton();
    QWidget::resizeEvent(e);
}

void ChatPage::updateScrollDownButton() {
    if (!scrollDownBtn_ || !msgScroll_) return;
    auto* sb = msgScroll_->verticalScrollBar();
    const bool show = sb->maximum() - sb->value() > 300;
    scrollDownBtn_->setVisible(show);
    if (show)
        scrollDownBtn_->move(msgScroll_->width() - 60,
                             msgScroll_->height() - 64);
}

void ChatPage::smoothScrollTo(int target) {
    if (!msgScroll_) return;
    auto* sb = msgScroll_->verticalScrollBar();
    if (scrollAnim_->state() == QAbstractAnimation::Running) scrollAnim_->stop();
    programmaticScroll_ = true;
    scrollAnim_->setStartValue(sb->value());
    scrollAnim_->setEndValue(qBound(sb->minimum(), target, sb->maximum()));
    scrollAnim_->start(QAbstractAnimation::DeleteWhenStopped);
    scrollAnim_ = new QVariantAnimation(this);
    scrollAnim_->setDuration(320);
    scrollAnim_->setEasingCurve(QEasingCurve::OutCubic);
    connect(scrollAnim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        msgScroll_->verticalScrollBar()->setValue(v.toInt());
    });
    connect(scrollAnim_, &QAbstractAnimation::finished, this, [this]() {
        programmaticScroll_ = false;
        updateScrollDownButton();
    });
}

void ChatPage::clampBubbleWidths() {
    if (!msgContainer_) return;
    const int maxW = qBound(260, msgContainer_->width() * 72 / 100, 480);
    const auto bubbles = msgContainer_->findChildren<QFrame*>();
    for (QFrame* b : bubbles)
        if (b->objectName() == QStringLiteral("bubbleIn")
            || b->objectName() == QStringLiteral("bubbleOut"))
            b->setMaximumWidth(maxW);
}

void ChatPage::keyPressEvent(QKeyEvent* e) {
    // Esc-каскад (KEY-01): верхний оверлей закрывается первым.
    if (e->key() == Qt::Key_Escape && consumeEscape()) {
        e->accept();
        return;
    }
    // Супер-поиск: Ctrl+Shift+F — как в веб-клиенте.
    if ((e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))
            == (Qt::ControlModifier | Qt::ShiftModifier)
        && e->key() == Qt::Key_F) {
        e->accept();
        openSuperSearch();
        return;
    }
    QWidget::keyPressEvent(e);
}

// ── Клавиатурная навигация (KEY-01..04) ──────────────────────────────────────

bool ChatPage::consumeEscape() {
    // Модалки ловят свой Esc сами, когда в фокусе; здесь — если фокус убежал.
    if (quickSwitcher_ && quickSwitcher_->isVisible()) { quickSwitcher_->closeAnimated(); return true; }
    if (superSearch_ && superSearch_->isVisible())     { superSearch_->hide(); return true; }
    if (appMenu_ && appMenu_->isVisible())             { closeAppMenu(); return true; }
    if (emojiPicker_ && emojiPicker_->isVisible())     { emojiPicker_->hide(); return true; }
    if (thirdColTarget_ > 0)                           { closeThirdColumn(); return true; }
    // Поиск в сайдбаре: Esc чистит запрос и возвращает фокус в переписку.
    if (search_ && !search_->text().isEmpty()) {
        search_->clear();
        if (composer_) composer_->setFocus();
        return true;
    }
    return false;
}

void ChatPage::cycleChat(int delta) {
    if (visibleChatIds_.isEmpty()) return;
    const int idx = visibleChatIds_.indexOf(currentPeerId_);
    // Не в списке (поиск человека/каталог) — начинаем с первого.
    const int next = idx < 0 ? (delta > 0 ? 0 : visibleChatIds_.size() - 1)
                             : (idx + delta + visibleChatIds_.size()) % visibleChatIds_.size();
    const QString& id = visibleChatIds_[next];
    const int ci = indexOfChat(id);
    if (ci >= 0) openChat(chats_[ci]);
}

void ChatPage::navigateChatHistory(int delta) {
    if (delta < 0) {   // Alt+← — назад
        if (navBack_.isEmpty()) return;
        const QString target = navBack_.takeLast();
        if (!currentPeerId_.isEmpty()) navForward_.append(currentPeerId_);
        navHistoryNavigating_ = true;   // openChat не должен писать в стек
        const int ci = indexOfChat(target);
        if (ci >= 0) openChat(chats_[ci]);
        else openChatWith(target, QString(), QString());
    } else {            // Alt+→ — вперёд
        if (navForward_.isEmpty()) return;
        const QString target = navForward_.takeLast();
        if (!currentPeerId_.isEmpty()) navBack_.append(currentPeerId_);
        navHistoryNavigating_ = true;
        const int ci = indexOfChat(target);
        if (ci >= 0) openChat(chats_[ci]);
        else openChatWith(target, QString(), QString());
    }
}

void ChatPage::editLastOwnMessage() {
    // Последнее своё в ОТРИСОВАННОМ хвосте — то, что видит пользователь.
    for (int i = currentMessages_.size() - 1; i >= 0; --i) {
        const ChatMessage& m = currentMessages_[i];
        if (!m.sent || m.id.startsWith(QStringLiteral("tmp")) || m.content.isEmpty()) continue;
        if (!findMessage(m.id)) continue;   // ещё не материализовано — идём дальше
        startEditing(m.id, m.content);
        return;
    }
}

// Тестовые швы (design-verify): закрытая навигация по имени действия.
void ChatPage::debugAction(const QString& name, int arg) {
    if (name == QLatin1String("open") && arg >= 0 && arg < chats_.size())
        openChat(chats_[arg]);
    else if (name == QLatin1String("navHistory")) navigateChatHistory(arg);
    else if (name == QLatin1String("cycleChat"))   cycleChat(arg);
    else if (name == QLatin1String("editLast"))    editLastOwnMessage();
    else if (name == QLatin1String("toggleArchiveSection")) {
        archiveOpen_ = !archiveOpen_;
        rebuildChatList();
    }
    else if (name == QLatin1String("archiveChat") && arg >= 0 && arg < chats_.size()) {
        const QString key = chatKeyFor(chats_[arg]);
        if (archivedChats_.contains(key)) archivedChats_.remove(key);
        else                              archivedChats_.insert(key);
        saveArchivedChats();
        rebuildChatList();
    }
}

int ChatPage::indexOfChat(const QString& id) const {
    for (int i = 0; i < chats_.size(); ++i)
        if (chats_[i].id == id) return i;
    return -1;
}

void ChatPage::onChatsLoaded(const QList<Chat>& chats) {
    personalChats_ = chats;
    mergeAllChats();
}

void ChatPage::mergeAllChats() {
    chats_.clear();
    chats_.reserve(personalChats_.size() + groupChats_.size() + channelChats_.size());
    chats_ += personalChats_;
    chats_ += groupChats_;
    chats_ += channelChats_;
    loadArchivedChats();   // локальный архив мог измениться с прошлого merge
    rebuildFolderStrip();   // обновить счётчики папок
    rebuildChatList();
}

// ── Архив (LST-03): локальные ключи «type:id» в Prefs, как мьюты ─────────────

void ChatPage::loadArchivedChats() {
    archivedChats_.clear();
    const QString raw = Prefs::getStr(QStringLiteral("xipher_archived_chats"));
    for (const QString& part : raw.split(QLatin1Char(','), Qt::SkipEmptyParts))
        archivedChats_.insert(part.trimmed());
}

void ChatPage::saveArchivedChats() {
    QStringList keys(archivedChats_.begin(), archivedChats_.end());
    keys.sort();
    Prefs::setStr(QStringLiteral("xipher_archived_chats"),
                  keys.join(QLatin1Char(',')) + QStringLiteral(","));
}

// ── Папки ─────────────────────────────────────────────────────────────────────
void ChatPage::rebuildFolderStrip() {
    if (!folderRailItems_) return;
    folderRail_->setVisible(!folders_.isEmpty());

    // Очистить плитки полностью (кнопка «＋» и stretch тоже — строим заново).
    while (folderRailItems_->count() > 0) {
        QLayoutItem* it = folderRailItems_->takeAt(0);
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    folderRailItems_->addStretch();   // растяжка между плитками и кнопкой «＋»
    if (folders_.isEmpty()) return;

    // Плитка рейла 1:1 с .folder-rail-item веба: иконка-плитка 38px (r12,
    // подкрашивается цветом папки) + подпись; непрочитанные — бейдж на углу.
    auto makeRailItem = [this](const QString& id, const QString& title,
                               const QString& icon, const QString& color, int unread) {
        const bool active = (id == activeFolderId_);
        auto* b = new QPushButton();
        b->setObjectName(active ? QStringLiteral("folderRailItemActive")
                                : QStringLiteral("folderRailItem"));
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(title);
        // Фиксированный размер: QPushButton со стилем не считает layout-минимум
        // детей — без фиксации кнопка сжимается и плитки обрезаются.
        b->setFixedSize(62, 74);
        auto* v = new QVBoxLayout(b);
        v->setContentsMargins(2, 6, 2, 6);
        v->setSpacing(4);

        auto* iconTile = new QLabel(FolderIcons::glyph(icon), b);
        iconTile->setObjectName(QStringLiteral("folderRailIcon"));
        iconTile->setAlignment(Qt::AlignCenter);
        iconTile->setFixedSize(38, 38);
        const QString base = color.isEmpty() ? QStringLiteral("#8B5CF6") : color;
        iconTile->setStyleSheet(QStringLiteral(
            "background:rgba(%1,%2,%3,0.18);border-radius:12px;font-size:17px;"
            "border:1px solid rgba(%1,%2,%3,0.30);")
            .arg(base.mid(1,2).toInt(nullptr,16))
            .arg(base.mid(3,2).toInt(nullptr,16))
            .arg(base.mid(5,2).toInt(nullptr,16)));
        v->addWidget(iconTile, 0, Qt::AlignHCenter);

        auto* label = new QLabel(title, b);
        label->setObjectName(QStringLiteral("folderRailLabel"));
        label->setAlignment(Qt::AlignCenter);
        v->addWidget(label);

        if (unread > 0) {
            auto* badge = new QLabel(unread > 99 ? QStringLiteral("99+")
                                                 : QString::number(unread), iconTile);
            badge->setObjectName(QStringLiteral("folderRailCount"));
            badge->setAlignment(Qt::AlignCenter);
            badge->move(22, -4);
            badge->adjustSize();
            badge->raise();
        }

        connect(b, &QPushButton::clicked, this, [this, id]() { setActiveFolder(id); });
        if (id != QStringLiteral("all")) {
            b->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(b, &QPushButton::customContextMenuRequested, this, [this, id, b](const QPoint&) {
                QMenu m(this);
                QAction* edit = m.addAction(QStringLiteral("Редактировать"));
                QAction* del  = m.addAction(QStringLiteral("Удалить"));
                QAction* ch = m.exec(b->mapToGlobal(QPoint(0, b->height())));
                if (ch == edit) openFolderEditor(id);
                else if (ch == del) {
                    folders_.erase(std::remove_if(folders_.begin(), folders_.end(),
                        [&](const Folder& f){ return f.id == id; }), folders_.end());
                    if (activeFolderId_ == id) setActiveFolder(QStringLiteral("all"));
                    api_->setChatFolders(folders_);
                    rebuildFolderStrip();
                }
            });
        }
        return b;
    };

    int unreadAll = 0;
    for (const Chat& c : chats_) unreadAll += c.unread;
    int idx = 0;
    folderRailItems_->insertWidget(idx++, makeRailItem(
        QStringLiteral("all"), QStringLiteral("Все"), QStringLiteral("chat"), QString(), unreadAll));
    for (const Folder& f : folders_) {
        int unread = 0, cnt = 0;
        const QSet<QString> keys(f.chatKeys.begin(), f.chatKeys.end());
        for (const Chat& c : chats_)
            if (keys.contains(chatKeyFor(c))) { ++cnt; unread += c.unread; }
        Q_UNUSED(cnt);
        folderRailItems_->insertWidget(idx++,
            makeRailItem(f.id, f.name, f.icon, f.color, unread));
    }

    // Кнопка «правка папок» внизу рейла — как .folder-rail-edit в вебе.
    auto* add = new QPushButton(QStringLiteral("＋"));
    add->setObjectName(QStringLiteral("folderRailEdit"));
    add->setCursor(Qt::PointingHandCursor);
    add->setToolTip(QStringLiteral("Настроить папки"));
    add->setFixedSize(38, 38);
    connect(add, &QPushButton::clicked, this, [this]() { openFolderEditor(QString()); });
    folderRailItems_->addWidget(add, 0, Qt::AlignHCenter);
}

void ChatPage::setActiveFolder(const QString& id) {
    activeFolderId_ = id;
    Prefs::setStr(QStringLiteral("xipher_active_folder"), id);
    rebuildFolderStrip();
    rebuildChatList();
}

void ChatPage::openFolderEditor(const QString& folderId) {
    Folder existing;
    for (const Folder& f : folders_) if (f.id == folderId) { existing = f; break; }
    auto* dlg = new FolderEditorDialog(chats_, existing, window());
    connect(dlg, &FolderEditorDialog::saved, this, [this](const Folder& f) {
        bool found = false;
        for (Folder& ex : folders_) if (ex.id == f.id) { ex = f; found = true; break; }
        if (!found) folders_.append(f);
        api_->setChatFolders(folders_);
        setActiveFolder(f.id);
    });
    connect(dlg, &FolderEditorDialog::removed, this, [this](const QString& id) {
        folders_.erase(std::remove_if(folders_.begin(), folders_.end(),
            [&](const Folder& f){ return f.id == id; }), folders_.end());
        if (activeFolderId_ == id) activeFolderId_ = QStringLiteral("all");
        api_->setChatFolders(folders_);
        setActiveFolder(activeFolderId_);
    });
    dlg->showAnimated();
}

// ── Форум-темы ────────────────────────────────────────────────────────────────
void ChatPage::showTopicsList(const QList<Topic>& topics) {
    currentTopics_ = topics;
    currentTopicId_.clear();
    if (topicBackBtn_) topicBackBtn_->setVisible(false);
    if (topicsTitle_) topicsTitle_->setText(currentPeerName_);
    convStack_->setCurrentIndex(2);

    while (topicsBox_->count() > 1) {
        QLayoutItem* it = topicsBox_->takeAt(0);
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    if (topics.isEmpty()) {
        auto* e = new QLabel(QStringLiteral("Тем пока нет. Создайте первую кнопкой +"));
        e->setStyleSheet(QStringLiteral("color:#726C82;font-size:13px;padding:16px;"));
        e->setWordWrap(true);
        topicsBox_->insertWidget(0, e);
        return;
    }
    int row = 0;
    for (const Topic& t : topics) {
        auto* w = new QFrame();
        w->setObjectName(QStringLiteral("topicRow"));
        w->setStyleSheet(QStringLiteral("#topicRow{background:#131218;border-radius:12px;}"
                                        "#topicRow:hover{background:#1A1822;}"));
        w->setCursor(Qt::PointingHandCursor);
        w->setProperty("topicIdx", row);
        w->installEventFilter(this);
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(12, 10, 12, 10);
        h->setSpacing(12);
        // Цветной кружок с эмодзи темы.
        auto* chip = new QLabel(t.iconEmoji.isEmpty() ? QStringLiteral("#") : t.iconEmoji);
        chip->setFixedSize(40, 40);
        chip->setAlignment(Qt::AlignCenter);
        const QString col = t.iconColor.isEmpty() ? QStringLiteral("#8B5CF6") : t.iconColor;
        chip->setStyleSheet(QStringLiteral("background:%1;border-radius:20px;font-size:18px;color:#fff;").arg(col));
        h->addWidget(chip);
        auto* col2 = new QVBoxLayout(); col2->setSpacing(2);
        QString nm = t.name;
        if (t.isClosed) nm += QStringLiteral("  🔒");
        if (t.pinnedOrder > 0) nm = QStringLiteral("📌 ") + nm;
        auto* nmL = new QLabel(nm); nmL->setStyleSheet(QStringLiteral("color:#F3F1F8;font-size:14px;font-weight:600;"));
        col2->addWidget(nmL);
        if (!t.lastMessage.isEmpty()) {
            auto* lm = new QLabel((t.lastSender.isEmpty() ? QString() : t.lastSender + QStringLiteral(": ")) + t.lastMessage);
            lm->setStyleSheet(QStringLiteral("color:#ACA6BD;font-size:12px;"));
            col2->addWidget(lm);
        }
        h->addLayout(col2, 1);
        if (t.unread > 0) {
            auto* b = new QLabel(QString::number(t.unread));
            b->setAlignment(Qt::AlignCenter);
            b->setStyleSheet(QStringLiteral("background:#8B5CF6;color:#fff;font-size:11px;font-weight:700;"
                "border-radius:9px;min-width:18px;min-height:18px;padding:0 5px;"));
            h->addWidget(b);
        }
        topicsBox_->insertWidget(row++, w);
    }
}

void ChatPage::openTopic(const Topic& topic) {
    currentTopicId_ = topic.id;
    if (topicBackBtn_) topicBackBtn_->setVisible(true);
    peerName_->setText(topic.name);
    peerStatus_->setText(QStringLiteral("тема • %1").arg(currentPeerName_));
    convStack_->setCurrentIndex(1);
    clearMessages();
    loadingChat_ = true;   // приветствие не мелькает, пока тема грузится
    renderCached();
    api_->getTopicMessages(topic.id);
}

void ChatPage::createTopicDialog() {
    if (currentPeerId_.isEmpty()) return;
    auto* ov = new ModalOverlay(window(), 380);
    ov->card()->setStyleSheet(QStringLiteral(
        "#modalCard{background:#17151E;border:1px solid rgba(255,255,255,0.08);border-radius:16px;}"
        "QLabel{color:#F3F1F8;} QLineEdit{background:#131218;border:1px solid rgba(255,255,255,0.10);"
        "border-radius:10px;min-height:38px;padding:0 12px;color:#F3F1F8;}"
        "QLineEdit:focus{border:1px solid #8B5CF6;}"
        "#primaryBtn{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #8B5CF6,stop:1 #6D28D9);"
        "color:#fff;border:none;border-radius:10px;min-height:38px;padding:0 20px;font-weight:700;}"));
    auto* t = new QLabel(QStringLiteral("Новая тема"), ov->card());
    t->setStyleSheet(QStringLiteral("font-size:17px;font-weight:800;"));
    ov->cardLayout()->addWidget(t);
    auto* nameEd = new QLineEdit(ov->card());
    nameEd->setPlaceholderText(QStringLiteral("Название темы"));
    ov->cardLayout()->addWidget(nameEd);
    auto* row = new QWidget(ov->card());
    auto* rh = new QHBoxLayout(row); rh->setContentsMargins(0,0,0,0);
    auto* create = new QPushButton(QStringLiteral("Создать"), row);
    create->setObjectName(QStringLiteral("primaryBtn")); create->setCursor(Qt::PointingHandCursor);
    rh->addStretch(); rh->addWidget(create);
    ov->cardLayout()->addWidget(row);
    const QString gid = currentPeerId_;
    connect(create, &QPushButton::clicked, this, [this, ov, nameEd, gid]() {
        const QString nm = nameEd->text().trimmed();
        if (nm.isEmpty()) return;
        // палитра цветов как в вебе
        static const char* cols[] = {"#6fb1fc","#ffa44e","#ff7a82","#b691ff","#ffbc5c","#7ed7a8"};
        api_->createGroupTopic(gid, nm, QStringLiteral("💬"),
                               QString::fromLatin1(cols[currentTopics_.size() % 6]));
        ov->closeAnimated();
    });
    ov->showAnimated();
    nameEd->setFocus();
}

// Строка контакта (аватар + имя + подзаголовок + опц. время/бейдж).
static QWidget* buildContactRow(const QString& url, const QString& avatarText,
                                const QString& title, const QString& subtitle,
                                const QString& time, int unread, bool pinned = false) {
    auto* row = new QWidget();
    row->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* rl = new QHBoxLayout(row);
    // Отступы 1:1 с .chat-item веба: 0.875rem 1.5rem (14px × 24px).
    rl->setContentsMargins(16, 10, 16, 10);
    rl->setSpacing(12);

    auto* av = new QLabel();
    Avatar::setRound(av, url, avatarText, 48);
    rl->addWidget(av);

    auto* mid = new QVBoxLayout();
    mid->setSpacing(2);
    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(6);
    auto* name = new QLabel(row);
    name->setStyleSheet(QStringLiteral("color:#F3F1F8;font-size:15px;font-weight:600;"));
    name->setText(elide(title, name->font(), 200));
    topRow->addWidget(name);
    topRow->addStretch();
    if (pinned) {
        // Индикатор закрепления (как .chat-pin-indicator веба): 📌 у времени.
        auto* pinIco = new QLabel(QStringLiteral("📌"), row);
        pinIco->setStyleSheet(QStringLiteral("color:#726C82;font-size:12px;"));
        topRow->addWidget(pinIco);
    }
    if (!time.isEmpty()) {
        auto* t = new QLabel(time, row);
        t->setStyleSheet(QStringLiteral("color:#726C82;font-size:12px;"));
        topRow->addWidget(t);
    }

    auto* botRow = new QHBoxLayout();
    botRow->setSpacing(6);
    auto* last = new QLabel(row);
    last->setStyleSheet(QStringLiteral("color:#ACA6BD;font-size:14px;"));
    last->setText(elide(subtitle, last->font(), 210));
    botRow->addWidget(last);
    botRow->addStretch();
    if (unread > 0) {
        // Бейдж 1:1 с .chat-unread: accent, r12, min-width 20.
        auto* badge = new QLabel(QString::number(unread), row);
        badge->setAlignment(Qt::AlignCenter);
        badge->setStyleSheet(QStringLiteral(
            "background:#8B5CF6;color:#fff;font-size:12px;font-weight:600;"
            "border-radius:12px;min-width:20px;padding:2px 8px;"));
        botRow->addWidget(badge);
    }
    mid->addLayout(topRow);
    mid->addLayout(botRow);
    rl->addLayout(mid, 1);
    return row;
}

void ChatPage::rebuildChatList() {
    const QString filter = search_->text().trimmed().toLower();
    chatList_->blockSignals(true);
    chatList_->clear();

    // Активная папка: ограничиваем список её ключами.
    QSet<QString> folderKeys;
    if (activeFolderId_ != QStringLiteral("all"))
        for (const Folder& f : folders_)
            if (f.id == activeFolderId_) { folderKeys = QSet<QString>(f.chatKeys.begin(), f.chatKeys.end()); break; }
    const bool folderActive = !folderKeys.isEmpty() || activeFolderId_ != QStringLiteral("all");

    QSet<QString> shownChatIds;
    visibleChatIds_.clear();   // порядок видимого списка (KEY-02: Ctrl+PgUp/Dn)
    // Пины — секцией сверху (LST-04, как sortChatsForDisplay веба):
    // закреплённые идут первыми, внутри секции — порядок сервера (~свежесть).
    QList<const Chat*> ordered;
    ordered.reserve(chats_.size());
    for (const Chat& c : chats_)
        if (pinnedChats_.contains(chatKeyFor(c))) ordered.append(&c);
    for (const Chat& c : chats_)
        if (!pinnedChats_.contains(chatKeyFor(c))) ordered.append(&c);
    for (const Chat* cp : ordered) {
        const Chat& c = *cp;
        if (archivedChats_.contains(chatKeyFor(c))) continue;   // архив — своей секцией
        if (folderActive && !folderKeys.contains(chatKeyFor(c))) continue;
        if (!filter.isEmpty() &&
            !c.displayName.toLower().contains(filter) &&
            !c.name.toLower().contains(filter))
            continue;
        shownChatIds.insert(c.id);
        visibleChatIds_.append(c.id);

        const QString avatarText = c.isSaved ? QStringLiteral("★")
                                  : (c.avatarText.isEmpty() ? c.displayName : c.avatarText);
        const QString time = c.time == QStringLiteral("Нет сообщений") ? QString() : c.time;
        auto* row = buildContactRow(c.isSaved ? QString() : c.avatarUrl,
                                    avatarText, c.displayName, chatPreview(c.lastMessage), time,
                                    c.unread, pinnedChats_.contains(chatKeyFor(c)));

        auto* item = new QListWidgetItem(chatList_);
        item->setSizeHint(QSize(0, 72));
        item->setData(Qt::UserRole, c.id);
        item->setData(Qt::UserRole + 1, false);   // не результат поиска
        chatList_->addItem(item);
        chatList_->setItemWidget(item, row);
        if (c.id == currentPeerId_) item->setSelected(true);
    }

    // Архивная секция (LST-03): свёрнутый блок «Архив (N)» внизу, клик — разворот.
    {
        QList<const Chat*> archived;
        for (const Chat& c : chats_) {
            if (!archivedChats_.contains(chatKeyFor(c))) continue;
            if (folderActive && !folderKeys.contains(chatKeyFor(c))) continue;
            if (!filter.isEmpty() &&
                !c.displayName.toLower().contains(filter) &&
                !c.name.toLower().contains(filter))
                continue;
            archived.append(&c);
        }
        if (!archived.isEmpty()) {
            auto* head = new QWidget();
            auto* hl = new QHBoxLayout(head);
            hl->setContentsMargins(16, 8, 16, 8);
            hl->setSpacing(8);
            auto* arrow = new QLabel(archiveOpen_ ? QStringLiteral("▾") : QStringLiteral("▸"), head);
            arrow->setStyleSheet(QStringLiteral("color:#726C82;font-size:12px;"));
            auto* title = new QLabel(
                QStringLiteral("Архив (%1)").arg(archived.size()), head);
            title->setStyleSheet(QStringLiteral("color:#ACA6BD;font-size:13px;"
                                                "font-weight:700;text-transform:uppercase;"));
            hl->addWidget(arrow);
            hl->addWidget(title);
            hl->addStretch();
            auto* hItem = new QListWidgetItem(chatList_);
            hItem->setSizeHint(QSize(0, 36));
            hItem->setData(Qt::UserRole, QStringLiteral("__archive__"));
            hItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            chatList_->addItem(hItem);
            chatList_->setItemWidget(hItem, head);

            if (archiveOpen_) {
                for (const Chat* cp : archived) {
                    const Chat& c = *cp;
                    const QString avatarText = c.isSaved ? QStringLiteral("★")
                                          : (c.avatarText.isEmpty() ? c.displayName : c.avatarText);
                    const QString time = c.time == QStringLiteral("Нет сообщений") ? QString() : c.time;
                    auto* row = buildContactRow(c.isSaved ? QString() : c.avatarUrl,
                                                avatarText, c.displayName,
                                                chatPreview(c.lastMessage), time, c.unread);
                    auto* item = new QListWidgetItem(chatList_);
                    item->setSizeHint(QSize(0, 72));
                    item->setData(Qt::UserRole, c.id);
                    item->setData(Qt::UserRole + 1, false);
                    chatList_->addItem(item);
                    chatList_->setItemWidget(item, row);
                    visibleChatIds_.append(c.id);
                }
            }
        }
    }

    // Глобальный поиск людей (как в Telegram): показываем найденных, кого ещё нет в чатах.
    if (!filter.isEmpty() && !searchHits_.isEmpty()) {
        bool headerAdded = false;
        for (const UserHit& u : searchHits_) {
            if (shownChatIds.contains(u.id)) continue;
            if (!headerAdded) {
                auto* h = new QListWidgetItem(chatList_);
                h->setFlags(Qt::NoItemFlags);
                h->setSizeHint(QSize(0, 28));
                auto* hl = new QLabel(QStringLiteral("  Люди"));
                hl->setStyleSheet(QStringLiteral("color:#726C82;font-size:11px;font-weight:700;"
                                                 "text-transform:uppercase;padding:6px 4px;"));
                chatList_->addItem(h);
                chatList_->setItemWidget(h, hl);
                headerAdded = true;
            }
            auto* row = buildContactRow(u.avatarUrl, u.displayName.isEmpty() ? u.username : u.displayName,
                                        u.displayName.isEmpty() ? u.username : u.displayName,
                                        QStringLiteral("@") + u.username, QString(), 0);
            auto* item = new QListWidgetItem(chatList_);
            item->setSizeHint(QSize(0, 72));
            item->setData(Qt::UserRole, u.id);
            item->setData(Qt::UserRole + 1, true);   // результат поиска
            item->setData(Qt::UserRole + 2, u.displayName.isEmpty() ? u.username : u.displayName);
            item->setData(Qt::UserRole + 3, u.username);
            chatList_->addItem(item);
            chatList_->setItemWidget(item, row);
        }
    }

    // Глобальный поиск: публичные каналы и группы из каталога (не только свои).
    if (!filter.isEmpty() && !directoryHits_.isEmpty()) {
        bool headerAdded = false;
        for (const DirectoryItem& d : directoryHits_) {
            if (shownChatIds.contains(d.id)) continue;
            if (!headerAdded) {
                auto* h = new QListWidgetItem(chatList_);
                h->setFlags(Qt::NoItemFlags);
                h->setSizeHint(QSize(0, 28));
                auto* hl = new QLabel(QStringLiteral("  Каналы и группы"));
                hl->setStyleSheet(QStringLiteral("color:#726C82;font-size:11px;font-weight:700;"
                                                 "text-transform:uppercase;padding:6px 4px;"));
                chatList_->addItem(h);
                chatList_->setItemWidget(h, hl);
                headerAdded = true;
            }
            const QString sub = d.isMember
                ? QStringLiteral("участник · %1").arg(d.membersCount)
                : QStringLiteral("%1 · %2").arg(
                    d.type == QStringLiteral("channel") ? QStringLiteral("канал") : QStringLiteral("группа"))
                  .arg(d.membersCount);
            auto* row = buildContactRow(d.avatarUrl, d.name, d.name, sub, QString(), 0);
            auto* item = new QListWidgetItem(chatList_);
            item->setSizeHint(QSize(0, 72));
            item->setData(Qt::UserRole, d.id);
            item->setData(Qt::UserRole + 1, 2);              // элемент каталога
            item->setData(Qt::UserRole + 2, d.type);         // channel | group
            item->setData(Qt::UserRole + 3, d.name);
            item->setData(Qt::UserRole + 4, d.isMember);
            chatList_->addItem(item);
            chatList_->setItemWidget(item, row);
        }
    }
    chatList_->blockSignals(false);
}

void ChatPage::onChatClicked() {
    auto* item = chatList_->currentItem();
    if (!item) return;
    const QString id = item->data(Qt::UserRole).toString();
    // Заголовок архива (LST-03): клик разворачивает/сворачивает секцию.
    if (id == QStringLiteral("__archive__")) {
        archiveOpen_ = !archiveOpen_;
        rebuildChatList();
        return;
    }
    const QVariant kind = item->data(Qt::UserRole + 1);
    if (kind.toInt() == 2) {   // публичный канал/группа из каталога
        const QString type = item->data(Qt::UserRole + 2).toString();
        const QString name = item->data(Qt::UserRole + 3).toString();
        if (item->data(Qt::UserRole + 4).toBool()) {
            const int idx = indexOfChat(id);
            if (idx >= 0) openChat(chats_[idx]);
            return;
        }
        pendingJoinId_ = id;
        api_->joinPublic(id, type);   // после publicJoined списки обновятся
        return;
    }
    if (kind.toBool()) {   // найденный человек → открыть новый чат
        openChatWith(id, item->data(Qt::UserRole + 2).toString(),
                     item->data(Qt::UserRole + 3).toString());
        return;
    }
    const int idx = indexOfChat(id);
    if (idx < 0) return;
    openChat(chats_[idx]);
}

void ChatPage::openChat(const Chat& chat) {
    // Тот же чат уже открыт — не перерисовываем (без дёрганья), просто к низу.
    if (currentPeerId_ == chat.id && currentKind_ == chat.kind
        && convStack_->currentIndex() != 0 && currentTopicId_.isEmpty()) {
        scrollToBottom();
        const int idxSame = indexOfChat(chat.id);
        if (idxSame >= 0 && chats_[idxSame].unread > 0) {
            chats_[idxSame].unread = 0;
            rebuildChatList();
        }
        return;
    }
    clearReplyTo();
    cancelEditing();
    clearStagedFiles();   // вложения принадлежат чату, куда их бросили (MLT-06)
    highlightQuery_.clear();   // подсветка поиска не переезжает в чужой чат (SRC-04)
    if (emojiPicker_) emojiPicker_->hide();   // панель не висит над чужим чатом
    // История переходов (KEY-03): обычный переход пишет предыдущий чат в стек,
    // Alt+←/→ ходит по нему и не пишет (это делает сама navigateChatHistory).
    if (!navHistoryNavigating_ && !currentPeerId_.isEmpty()
            && currentPeerId_ != chat.id) {
        navBack_.append(currentPeerId_);
        if (navBack_.size() > 64) navBack_.removeFirst();
        navForward_.clear();   // новая ветка — «вперёд» сбрасывается (как в браузере)
    }
    navHistoryNavigating_ = false;
    saveDraft();                              // черновик предыдущего чата — в Prefs
    if (typingTimer_) typingTimer_->stop();
    peerStatusBase_.clear();
    currentPeerId_   = chat.id;
    draftKey_ = QStringLiteral("draft:") + chat.id;
    currentPeerName_ = chat.displayName;
    currentKind_     = chat.kind;
    peerName_->setText(chat.displayName);
    Avatar::setRound(peerAvatar_, chat.isSaved ? QString() : chat.avatarUrl,
                     chat.isSaved ? QStringLiteral("★") : chat.displayName, 40);
    QString status;
    if (chat.kind == ChatKind::Group)
        status = chat.membersCount > 0 ? QStringLiteral("%1 участников").arg(chat.membersCount)
                                       : QStringLiteral("Группа");
    else if (chat.kind == ChatKind::Channel)
        status = QStringLiteral("Канал");
    else status = chat.isSaved ? QStringLiteral("Заметки для себя")
                               : (chat.online ? QStringLiteral("в сети") : QStringLiteral("не в сети"));
    peerStatus_->setText(status);
    setThirdColumnInfo();   // третья колонка: инфо нового чата (если открыта)
    currentForum_ = false;
    currentTopicId_.clear();
    currentCanManage_ = (chat.role == QStringLiteral("creator") || chat.role == QStringLiteral("owner")
                         || chat.role == QStringLiteral("admin"));
    if (topicBackBtn_) topicBackBtn_->setVisible(false);
    // Пагинация истории заново: сервер отдаёт хвост (50), остальное —
    // догрузка при прокрутке к верху.
    hasMoreServer_ = true;
    fetchingOlder_ = false;
    pendingJumpId_.clear();
    hideBotKeyboard();
    currentReplyKeyboard_ = QJsonObject();

    // Пока нет ни кэша, ни ответа сервера — приветствие не мелькает.
    loadingChat_ = true;
    if (chat.kind == ChatKind::Group) {
        // Группа: сначала узнаём, форум ли это (тогда покажем список тем).
        api_->getGroupTopics(chat.id);
    } else if (chat.kind == ChatKind::Channel) {
        convStack_->setCurrentIndex(1); clearMessages(); renderCached();
        api_->getChannelMessages(chat.id, /*limit*/ 50);
    } else {
        convStack_->setCurrentIndex(1); clearMessages(); renderCached();
        restoreDraft();
    api_->getMessages(chat.id, /*limit*/ 50);
    }

    // Сброс непрочитанных в списке
    const int idx = indexOfChat(chat.id);
    if (idx >= 0 && chats_[idx].unread > 0) {
        chats_[idx].unread = 0;
        rebuildChatList();
    }
}

// ── Пины чатов (LST-04) ───────────────────────────────────────────────────────

void ChatPage::setChatPinned(const Chat& c, bool pinned) {
    const QString key = chatKeyFor(c);
    const bool was = pinnedChats_.contains(key);
    if (pinned == was) return;
    if (pinned && !was) {
        // Лимит как в вебе: 3 бесплатно, 10 с Xipher Pulse (сервер тоже проверяет).
        const int limit = Session::instance().isPremium ? 10 : 3;
        if (pinnedChats_.size() >= limit) {
            QMessageBox::information(this, QStringLiteral("Закрепления"),
                QStringLiteral("Лимит закреплений: %1 (с Xipher Pulse — 10).").arg(limit));
            return;
        }
    }
    // Оптимистично: секция обновляется сразу, сервер подтверждает молча.
    if (pinned) pinnedChats_.insert(key);
    else        pinnedChats_.remove(key);
    rebuildChatList();
    const QString type = c.isSaved ? QStringLiteral("saved")
                       : c.kind == ChatKind::Group   ? QStringLiteral("group")
                       : c.kind == ChatKind::Channel ? QStringLiteral("channel")
                                                     : QStringLiteral("chat");
    api_->setChatPinned(c.id, type, pinned);
}

void ChatPage::clearMessages() {
    // Останавливаем воспроизведение — виджеты ниже будут удалены.
    player_->stop();
    activeVoice_ = nullptr;
    // Видео: гасим плееры ДО deleteLater — иначе доигрывают «в невидимости».
    for (auto* vm : msgContainer_->findChildren<VideoMessageWidget*>())
        vm->stopPlayback();
    pendingPlayPath_.clear();

    QLayoutItem* it;
    while ((it = msgLayout_->takeAt(0)) != nullptr) {
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    msgLayout_->addStretch();
    shownIds_.clear();
    checklistWidgets_.clear();
    mergedChecklists_.clear();
    pendingImage_.clear();   // виджеты-картинки удалены — не держим устаревшие ключи
    bubbleCount_ = 0;
    updateGreeting();
}

void ChatPage::updateGreeting() {
    if (!greeting_) return;
    // Пока история грузится (кэш пуст, ответ не пришёл) — не показываем:
    // приветствие мелькает на доли секунды и бесит. Только подтверждённо пустой чат.
    const bool show = !currentPeerId_.isEmpty() && bubbleCount_ == 0 && !loadingChat_;
    if (show) {
        greeting_->setGeometry(msgScroll_->viewport()->rect());
        greeting_->raise();
        greeting_->show();
    } else {
        greeting_->hide();
    }
}

// Определён ниже по файлу; нужен раньше (профиль → «Общие медиа»).
static void showInfoOverlay(QWidget* host, const QString& title, const QString& text);

bool ChatPage::eventFilter(QObject* obj, QEvent* e) {
    // Esc в поле ввода закрывает панель эмодзи (не очищает черновик).
    if (obj == composer_ && e->type() == QEvent::KeyPress
        && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape && emojiPicker_
        && emojiPicker_->isVisible()) {
        emojiPicker_->hide();
        return true;
    }
    if (obj == appMenuScrim_ && e->type() == QEvent::MouseButtonPress) {
        closeAppMenu();
        return true;
    }
    // Клик по скриму оверлей-режима третьей колонки — закрыть (WIN-01).
    if (obj == overlayScrim_ && e->type() == QEvent::MouseButtonPress) {
        closeThirdColumn();
        return true;
    }
    if (greeting_ && obj == msgScroll_->viewport() && e->type() == QEvent::Resize) {
        if (greeting_->isVisible()) greeting_->setGeometry(msgScroll_->viewport()->rect());
    }
    // Клик по картинке-сообщению → просмотр на весь экран.
    if (e->type() == QEvent::MouseButtonRelease
        && static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton) {
        if (auto* w = qobject_cast<QWidget*>(obj)) {
            const QVariant fv = w->property("imgFull");
            const QVariant fp = w->property("filePath");
            if (fv.isValid() && fv.canConvert<QPixmap>() && !fv.value<QPixmap>().isNull()) {
                // Как в Telegram: клик по фото — вся галерея чата с этой позиции.
                QStringList paths;
                int startIdx = 0;
                for (const ChatMessage& m : currentMessages_) {
                    if ((m.messageType == QStringLiteral("image")
                         || m.messageType == QStringLiteral("photo"))
                        && !m.filePath.isEmpty()) {
                        if (m.filePath == fp.toString()) startIdx = paths.size();
                        paths.append(m.filePath);
                    }
                }
                if (paths.size() <= 1) { ImageViewer::show(window(), fv.value<QPixmap>()); return true; }
                ImageViewer::showGallery(window(), paths, startIdx,
                    [this](const QString& p) { return mediaBytes(p); },
                    [this](const QString& p) { api_->fetchFile(p); });
                return true;
            }
            // Клик по теме форума.
            const QVariant ti = w->property("topicIdx");
            if (ti.isValid()) {
                const int idx = ti.toInt();
                if (idx >= 0 && idx < currentTopics_.size()) openTopic(currentTopics_[idx]);
                return true;
            }
            // Клик по заголовку списка тем → инфо группы.
            if (w->property("openPeerInfo").toBool() && !currentPeerId_.isEmpty()) {
                int gi = indexOfChat(currentPeerId_);
                QString av = gi >= 0 ? chats_[gi].avatarUrl : QString();
                auto* info = new PeerInfoPanel(api_, currentPeerId_, false, currentPeerName_, av, window());
                connect(info, &PeerInfoPanel::changed, this, [this]() { api_->getGroups(); });
                info->showAnimated();
                return true;
            }
            // Клик по имени автора в баббле → профиль 1:1 с вебом.
            const QVariant prof = w->property("openProfileFor");
            if (prof.isValid()) {
                ensureProfilePanel();
                profilePanel_->setKnownPreview(
                    w->property("senderName").toString(), QString(), false);
                profilePanel_->openFor(prof.toString());
                return true;
            }
        }
    }

    // ПКМ по теме форума → управление (для админов/создателя).
    if (e->type() == QEvent::ContextMenu) {
        if (auto* w = qobject_cast<QWidget*>(obj)) {
            const QVariant ti = w->property("topicIdx");
            if (ti.isValid() && currentCanManage_) {
                const int idx = ti.toInt();
                if (idx < 0 || idx >= currentTopics_.size()) return true;
                const Topic t = currentTopics_[idx];
                QMenu menu(this);
                QAction* rename = menu.addAction(QStringLiteral("Переименовать"));
                QAction* close = menu.addAction(t.isClosed ? QStringLiteral("Открыть тему") : QStringLiteral("Закрыть тему"));
                QAction* del = t.isGeneral ? nullptr : menu.addAction(QStringLiteral("Удалить тему"));
                QAction* ch = menu.exec(static_cast<QContextMenuEvent*>(e)->globalPos());
                if (ch == rename) {
                    bool okk = false;
                    const QString nm = QInputDialog::getText(this, QStringLiteral("Тема"),
                        QStringLiteral("Новое название"), QLineEdit::Normal, t.name, &okk);
                    if (okk && !nm.trimmed().isEmpty())
                        api_->updateGroupTopic(t.id, nm.trimmed(), QString(), QString(), -1);
                } else if (ch == close) {
                    api_->updateGroupTopic(t.id, QString(), QString(), QString(), t.isClosed ? 0 : 1);
                } else if (del && ch == del) {
                    api_->deleteGroupTopic(t.id);
                }
                return true;
            }
        }
    }

    // Клик по шапке диалога → третья колонка с инфо (WIN-01); полные карточки
    // (профиль/управление группой) — кнопками внутри колонки.
    if (obj == peerHeader_ && e->type() == QEvent::MouseButtonRelease && !currentPeerId_.isEmpty()) {
        if (thirdColTarget_ > 0)
            closeThirdColumn();
        else
            setThirdColumnOpen(true);
        return true;
    }
    return QWidget::eventFilter(obj, e);
}

// Единая точка создания ProfilePanel: связи в одном месте, обе кнопки
// открытия (шапка диалога, автор поста) получают одинаковый набор.
void ChatPage::ensureProfilePanel() {
    if (profilePanel_) return;
    profilePanel_ = new ProfilePanel(api_, window());
    // ModalOverlay самоудаляется после закрытия — сбрасываем кэш
    // (иначе второй клик — краш в удалённом объекте).
    connect(profilePanel_, &ModalOverlay::closed, this,
            [this]() { profilePanel_ = nullptr; });
    connect(profilePanel_, &ProfilePanel::messageRequested, this,
            [this](const QString& uid) {
        if (uid != currentPeerId_) {
            const int idx = indexOfChat(uid);
            if (idx >= 0) openChat(chats_[idx]);
        }
    });
    connect(profilePanel_, &ProfilePanel::callRequested, this,
            [this](const QString& uid, const QString& name, const QString& av) {
        emit callRequested(uid, name, av);
    });
    connect(profilePanel_, &ProfilePanel::savedMessagesRequested, this, [this]() {
        for (const Chat& c : chats_) if (c.isSaved) { openChat(c); return; }
        openChatWith(Session::instance().userId, QStringLiteral("Избранное"),
                     Session::instance().username);
    });
    connect(profilePanel_, &ProfilePanel::settingsRequested, this,
            &ChatPage::openSettings);
    // Канал из профиля: открыт — переключаемся, нет — вступаем (как каталог).
    connect(profilePanel_, &ProfilePanel::channelOpenRequested, this,
            [this](const QString& id, const QString& name, const QString&) {
        const int idx = indexOfChat(id);
        if (idx >= 0) { openChat(chats_[idx]); return; }
        pendingJoinId_ = id;
        api_->joinPublic(id, QStringLiteral("channel"));
    });
    connect(profilePanel_, &ProfilePanel::mediaRequested, this,
            [this](const QString&, int total) {
        showInfoOverlay(window(), QStringLiteral("Общие медиа"),
            total > 0 ? QStringLiteral("Общих медиа: %1").arg(total)
                      : QStringLiteral("Нет общих медиа"));
    });
}

void ChatPage::applyMessages(const QList<ChatMessage>& messages, bool updateCache) {
    loadingChat_ = false;
    // Пустой/запоздалый ответ сервера не должен стирать уже открытый вид:
    // если ответ на догрузку СТАРЫХ (before_id) — его ведёт onMessagesLoaded,
    // а пустая «полная» история при живом виде — мусор/гонка, игнорируем.
    if (messages.isEmpty() && bubbleCount_ > 0 && !fetchingOlder_) return;
    QList<ChatMessage> sorted = messages;
    std::sort(sorted.begin(), sorted.end(),
              [](const ChatMessage& a, const ChatMessage& b) { return a.createdAt < b.createdAt; });

    // Пользователь читает историю (ушёл от низа): ПОЛНАЯ перерисовка сорвала бы
    // позицию просмотра (тот самый «пиздец» при листании, когда приходит новое
    // сообщение). Подмешиваем только новые сообщения живыми бабблами.
    if (bubbleCount_ > 0 && !stickBottom_) {
        bool added = false;
        for (const ChatMessage& m : sorted) {
            if (m.id.isEmpty() || shownIds_.contains(m.id)) continue;
            if (m.content.startsWith(ChecklistProto::kUpdatePrefix)) continue;
            shownIds_.insert(m.id);
            currentMessages_.append(m);
            addBubble(m);
            added = true;
        }
        if (updateCache && added) cacheCurrent();
        return;
    }

    currentMessages_ = sorted;
    renderMessages(QString());
    if (updateCache) cacheCurrent();
    // Последняя reply-клавиатура бота в истории — активная (как в Telegram).
    for (int i = currentMessages_.size() - 1; i >= 0; --i) {
        const QJsonObject mk = currentMessages_[i].replyMarkup;
        if (!mk.isEmpty() && mk.contains(QStringLiteral("keyboard"))) {
            applyReplyKeyboard(mk);
            break;
        }
        if (!mk.isEmpty() && mk.contains(QStringLiteral("remove_keyboard"))) {
            hideBotKeyboard();
            break;
        }
    }
}

QString ChatPage::cacheKey() const {
    // Тема форума — отдельный поток сообщений со своим ключом кэша.
    return currentTopicId_.isEmpty() ? currentPeerId_
                                     : QStringLiteral("t:") + currentTopicId_;
}

void ChatPage::renderCached() {
    // Мгновенное открытие: показываем зашифрованный локальный кэш, не дожидаясь
    // ответа сервера; при приходе истории applyMessages перерисует поверх.
    const QString key = cacheKey();
    if (key.isEmpty()) return;
    const QList<ChatMessage> cached = ChatCache::instance().load(key);
    if (!cached.isEmpty()) applyMessages(cached, /*updateCache*/ false);
}

void ChatPage::cacheCurrent() {
    const QString key = cacheKey();
    if (!key.isEmpty()) ChatCache::instance().save(key, currentMessages_);
}

void ChatPage::onMessagesLoaded(const QString& friendId, const QList<ChatMessage>& messages) {
    if (friendId != currentPeerId_) return;
    if (!currentTopicId_.isEmpty()) return;   // внутри темы рендерим только её сообщения

    // Ответ на запрос СТАРЫХ сообщений (before_id): приклеиваем сверху,
    // дедуп по id, затем prepend-батчем достраиваем виджеты.
    if (fetchingOlder_) {
        fetchingOlder_ = false;
        if (messages.size() < 50) hasMoreServer_ = false;
        QSet<QString> known;
        for (const ChatMessage& m : currentMessages_) known.insert(m.id);
        const int firstRendered = renderedFrom_;
        QList<ChatMessage> older;
        for (const ChatMessage& m : messages)
            if (!m.id.isEmpty() && !known.contains(m.id)) older.append(m);
        if (older.isEmpty()) return;
        currentMessages_ = older + currentMessages_;
        std::sort(currentMessages_.begin(), currentMessages_.end(),
                  [](const ChatMessage& a, const ChatMessage& b) { return a.createdAt < b.createdAt; });
        renderedFrom_ = firstRendered + older.size();
        cacheCurrent();
        prependOlderBatch(0, older.size());
        tryJumpToPending();
        return;
    }
    applyMessages(messages);
}

void ChatPage::renderMessages(const QString& filter) {
    Q_UNUSED(filter);   // фильтр в чате ушёл в единый поиск (SuperSearchDialog)
    clearMessages();
    stickBottom_ = true;   // перерисовка = заново открываем чат: вид снизу
    QString lastDay;
    // Как в Telegram: в виджетах — только ХВОСТ истории (50 сообщений).
    // Остальное догружается виджетами при прокрутке к верху (prependOlderMessages),
    // данные при этом и так целиком в currentMessages_ (либо с сервера — before_id).
    const int total = currentMessages_.size();
    const int from = qMax(0, total - 50);
    // tdesktop-style: СИНХРОННО строим только видимый хвост, остальной хвост —
    // чанками в простое (прерывается при смене чата).
    const int firstShown = qMax(from, total - 14);
    renderedFrom_ = firstShown;
    for (int i = 0; i < total; ++i) {
        const ChatMessage& m = currentMessages_[i];
        if (!m.id.isEmpty()) shownIds_.insert(m.id);   // дедуп работаем по всей истории
        if (i < firstShown) continue;
        const QString day = m.createdAt.left(10);
        if (!day.isEmpty() && day != lastDay) {
            const QString lbl = dateLabel(m.createdAt);
            if (!lbl.isEmpty()) msgLayout_->addWidget(makeDateSeparator(lbl));
            lastDay = day;
        }
        addBubble(m);
    }
    updateGreeting();
    // Достройка хвоста чанками (tdesktop: ленивое построение за границей экрана).
    if (renderedFrom_ > from) {
        const quint64 gen = ++renderGen_;
        QTimer::singleShot(0, this, [this, gen, from] { continueTail(from, gen); });
    }
    scrollToBottom();
    // Если это был перезапуск после прыжка из поиска — пробуем прыгнуть.
    tryJumpToPending();
}

void ChatPage::continueTail(int targetFrom, quint64 gen) {
    if (gen != renderGen_) return;                 // чат сменился — цепочка отменена
    if (renderedFrom_ <= targetFrom) { tryJumpToPending(); return; }   // хвост достроен
    prependOlderBatch(targetFrom, 12);
    QTimer::singleShot(0, this, [this, targetFrom, gen] { continueTail(targetFrom, gen); });
}

// Порция prepend'а старых сообщений. Контент растёт СВЕРХУ — без якоря
// позиция просмотра уезжает вниз ровно на высоту порции (тот самый «съезд»
// при листании сразу после входа в чат). Якорь: value += прирост высоты.
// ВАЖНО: прирост меряем ПОСЛЕ синхронного пересчёта геометрии (adjustSize),
// иначе maximum() ещё старый, grown = 0, и вид дёргается.
void ChatPage::prependOlderBatch(int floorFrom, int batch) {
    if (renderedFrom_ <= floorFrom) return;
    auto* sb = msgScroll_->verticalScrollBar();
    const int oldMax = sb->maximum();
    const int oldVal = sb->value();

    const int from = qMax(floorFrom, renderedFrom_ - batch);
    QString lastDay = currentMessages_.value(renderedFrom_).createdAt.left(10);
    for (int i = from; i < renderedFrom_; ++i) {
        const ChatMessage& m = currentMessages_[i];
        if (!m.id.isEmpty()) shownIds_.insert(m.id);
        if (!m.createdAt.isEmpty()) {
            const QString day = m.createdAt.left(10);
            if (day != lastDay) {
                const QString lbl = dateLabel(m.createdAt);
                if (!lbl.isEmpty()) msgLayout_->insertWidget(1, makeDateSeparator(lbl));
                lastDay = day;
            }
        }
        addBubble(m, /*prepend=*/true);
        if (!m.createdAt.isEmpty()) lastDay = m.createdAt.left(10);
    }
    renderedFrom_ = from;

    // Синхронный пересчёт: layout активируется сразу, QScrollArea успевает
    // обновить диапазон — компенсация точная, без «пиздеца» при листании.
    msgContainer_->adjustSize();
    const int grown = sb->maximum() - oldMax;
    if (grown > 0) {
        programmaticScroll_ = true;
        sb->setValue(oldVal + grown);
        programmaticScroll_ = false;
    }
    updateScrollDownButton();
}

// Догрузка старых сообщений при прокрутке к верху (Telegram-style):
// 1) есть ещё неотрисованные виджеты в currentMessages_ — строим батч;
// 2) локальные исчерпаны, но на сервере есть история — запрос before_id.
void ChatPage::prependOlderMessages() {
    if (loadingOlder_ || currentPeerId_.isEmpty()) return;
    if (renderedFrom_ > 0) {
        loadingOlder_ = true;
        prependOlderBatch(qMax(0, renderedFrom_ - 50), 50);
        loadingOlder_ = false;
        tryJumpToPending();
        return;
    }
    // Локальные данные исчерпаны — тянем старые с сервера (пагинация before_id,
    // та же, что у веб-клиента). Группы тянутся целиком при открытии — не дёргаем.
    if (hasMoreServer_ && !fetchingOlder_ && !currentMessages_.isEmpty()
        && currentKind_ != ChatKind::Group) {
        QString oldestId;
        for (const ChatMessage& m : currentMessages_)
            if (!m.id.isEmpty() && !m.id.startsWith(QStringLiteral("tmp_"))) { oldestId = m.id; break; }
        if (oldestId.isEmpty()) { hasMoreServer_ = false; return; }
        fetchingOlder_ = true;
        if (currentKind_ == ChatKind::Channel)
            api_->getChannelMessages(currentPeerId_, 50, oldestId);
        else
            api_->getMessages(currentPeerId_, 50, oldestId);
    }
}

void ChatPage::fetchOlderFromServer() {
    // Оставлено как семантическая точка входа; логика в prependOlderMessages.
    prependOlderMessages();
}

void ChatPage::addBubble(const ChatMessage& msg, bool prepend, bool animate) {
    // Апдейт чек-листа — не рисуем бабблом, применяем к существующему виджету.
    if (msg.content.startsWith(ChecklistProto::kUpdatePrefix)) {
        const QJsonObject upd = parseChecklistUpdate(msg.content);
        const QString clId = upd.value(QStringLiteral("checklistId")).toString();
        // сохраняем накопленное состояние (на случай, если виджет ещё не создан)
        QJsonObject merged = mergedChecklists_.value(clId);
        if (!merged.isEmpty() || checklistWidgets_.contains(clId)) {
            if (auto* w = checklistWidgets_.value(clId)) w->applyUpdate(upd);
        }
        return;
    }

    // В КАНАЛЕ все посты идут слева (как в Telegram), включая свои: канал —
    // широковещательная лента, а не диалог. out=false → баббл «входящий».
    const bool out = msg.sent && currentKind_ != ChatKind::Channel;

    auto* row = new QWidget(msgContainer_);
    row->setObjectName(QStringLiteral("msgRow"));
    row->setStyleSheet(QStringLiteral("#msgRow{background:transparent;}"));
    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(0);

    auto* bubble = new QFrame(row);
    bubble->setObjectName(out ? QStringLiteral("bubbleOut") : QStringLiteral("bubbleIn"));
    // Максимум 480px, но не более 72% ширины области сообщений (узкое окно).
    bubble->setMaximumWidth(qBound(260, msgContainer_->width() * 72 / 100, 480));
    // Чистое медиа без текста — без фона баббла (.is-media-only веба).
    const bool mediaOnly = (msg.messageType == QStringLiteral("image")
        || msg.messageType == QStringLiteral("photo")
        || msg.messageType == QStringLiteral("video")
        || msg.messageType == QStringLiteral("video_note"))
        && msg.content.isEmpty();
    if (mediaOnly)
        bubble->setStyleSheet(QStringLiteral(
            "#bubbleOut,#bubbleIn{background:transparent;border:none;padding:0;}"));
    else
        bubble->setStyleSheet(out ? bubbleOutQss() : bubbleInQss());
    auto* bl = new QVBoxLayout(bubble);
    bl->setContentsMargins(14, 8, 14, 6);
    bl->setSpacing(2);
    if (mediaOnly) bl->setContentsMargins(0, 0, 0, 2);   // медиа без полей баббла

    // Контекстное меню сообщения (ответить / копировать / удалить).
    bubble->setContextMenuPolicy(Qt::CustomContextMenu);
    bubble->setProperty("msgId", msg.id);
    bubble->setProperty("msgText", msg.content);
    bubble->setProperty("msgId", msg.id);
    bubble->setProperty("msgSent", msg.sent);
    bubble->setProperty("msgAuthor", msg.sent ? QStringLiteral("Вы")
                        : (msg.senderName.isEmpty() ? currentPeerName_ : msg.senderName));
    connect(bubble, &QWidget::customContextMenuRequested, this, [this, bubble](const QPoint& p) {
        showMessageMenu(bubble, bubble->mapToGlobal(p));
    });

    // Имя отправителя в группах и каналах (входящие, как в Telegram; в личке
    // имя не выводится). Клик по имени — профиль автора (1:1 с вебом).
    if (!out && (currentKind_ == ChatKind::Group || currentKind_ == ChatKind::Channel)
        && !msg.senderName.isEmpty() && msg.senderId != Session::instance().userId) {
        auto* author = new QLabel(msg.senderName, bubble);
        author->setStyleSheet(QStringLiteral("color:#8B5CF6;font-size:12px;font-weight:700;"));
        author->setCursor(Qt::PointingHandCursor);
        author->setProperty("openProfileFor", msg.senderId);
        author->setProperty("senderName", msg.senderName);   // превью шапки профиля
        author->installEventFilter(this);
        bl->addWidget(author);
    }

    // Reply-превью (если есть)
    if (!msg.replySnippet.isEmpty()) {
        auto* reply = new QLabel(
            QStringLiteral("%1\n%2").arg(msg.replyAuthor, elide(msg.replySnippet, font(), 360)), bubble);
        reply->setStyleSheet(QStringLiteral(
            "color:#BBA4FF;font-size:12px;border-left:2px solid #8B5CF6;padding-left:8px;"));
        bl->addWidget(reply);
    }

    if (msg.content.startsWith(kCallEventPrefix)) {
        // Лог звонка (как в Telegram): иконка телефона + подпись, клик — перезвонить
        // (buildCallEventElement в вебе делает плашку tappable).
        auto* crow = new QWidget(bubble);
        crow->setStyleSheet(QStringLiteral("background:transparent;"));
        crow->setCursor(Qt::PointingHandCursor);
        crow->setToolTip(QStringLiteral("Позвонить"));
        auto* cl = new QHBoxLayout(crow);
        cl->setContentsMargins(0, 2, 0, 2);
        cl->setSpacing(10);
        auto* ic = new QLabel(crow);
        ic->setPixmap(Icons::pixmap(Icons::Phone, 20,
            out ? QColor(0xF0, 0xEC, 0xFA) : QColor(0x8B, 0x5C, 0xF6)));
        auto* lbl = new QLabel(callEventLabel(msg.content, msg.sent), crow);
        lbl->setStyleSheet(QString("color:%1;font-size:14px;font-weight:600;")
            .arg(out ? QStringLiteral("#F0ECFA") : QStringLiteral("#F3F1F8")));
        cl->addWidget(ic);
        cl->addWidget(lbl);
        cl->addStretch();
        bl->addWidget(crow);
        const QString peer = out ? currentPeerId_ : msg.senderId;
        const QString nm = out ? currentPeerName_ : msg.senderName;
        crow->installEventFilter(new SuperSearchClickFilter([this, peer, nm]() {
            emit callRequested(peer, nm, QString());
        }, crow));
    } else if (msg.isVoice()) {
        // Голосовое (Telegram-style): ▶/⏸ + waveform + время.
        const QString seed = msg.id.isEmpty() ? msg.filePath : msg.id;
        auto* voice = new VoiceMessageWidget(seed, out, bubble);
        voice->setMinimumWidth(240);
        bubble->setMinimumWidth(280);
        const int vsecs = parseVoiceSeconds(msg.content);   // длительность из подписи
        if (vsecs > 0) voice->setTotalMs(qint64(vsecs) * 1000);
        bl->addWidget(voice);
        const QString path = msg.filePath;
        connect(voice, &VoiceMessageWidget::playPauseClicked, this,
                [this, voice, path]() { onVoicePlayPause(voice, path); });
        voice->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(voice, &QWidget::customContextMenuRequested, this, [this, voice, path](const QPoint& p) {
            showMediaMenu(voice, path, QStringLiteral("voice"), p);
        });
        connect(voice, &VoiceMessageWidget::seekRequested, this, [this, voice](qreal frac) {
            if (activeVoice_ == voice && player_->duration() > 0)
                player_->setPosition(qint64(frac * player_->duration()));
        });
    } else if (msg.content.startsWith(ChecklistProto::kPrefix)) {
        // Чек-лист (как в вебе): [[XIPHER_CHECKLIST]] + JSON.
        QJsonObject pl = parseChecklist(msg.content);
        const QString clId = pl.value(QStringLiteral("id")).toString();
        if (mergedChecklists_.contains(clId))   // применяем накопленные апдейты
            pl = mergedChecklists_.value(clId);
        const bool canMark = msg.sent || pl.value(QStringLiteral("othersCanMark")).toBool(true);
        auto* cl = new ChecklistWidget(pl, msg.sent, canMark, bubble);
        bubble->setMinimumWidth(300);
        bl->addWidget(cl);
        checklistWidgets_.insert(clId, cl);
        const QString peer = currentPeerId_;
        connect(cl, &ChecklistWidget::itemToggled, this, [this, clId, peer](const QString& itemId, bool done) {
            QJsonObject upd{{QStringLiteral("checklistId"), clId},
                            {QStringLiteral("updates"), QJsonArray{QJsonObject{
                                {QStringLiteral("id"), itemId}, {QStringLiteral("done"), done}}}}};
            const QString content = ChecklistProto::kUpdatePrefix +
                QString::fromUtf8(QJsonDocument(upd).toJson(QJsonDocument::Compact));
            api_->sendRaw(peer, content, QStringLiteral("text"),
                          QStringLiteral("clu_%1").arg(++tempCounter_));
        });
    } else if (msg.messageType == QStringLiteral("location") ||
               msg.messageType == QStringLiteral("live_location") || isGeoContent(msg.content)) {
        // Геопозиция: кнопка-карточка, открывает карту в браузере.
        auto* geo = new QPushButton(bubble);
        geo->setCursor(Qt::PointingHandCursor);
        geo->setIcon(Icons::icon(Icons::Location, 20,
            out ? QColor(0xF0,0xEC,0xFA) : QColor(0x8B,0x5C,0xF6)));
        geo->setIconSize(QSize(20, 20));
        geo->setText(QStringLiteral("  Геопозиция\n  Открыть на карте"));
        geo->setStyleSheet(QString(
            "QPushButton{border:none;background:transparent;text-align:left;font-size:14px;color:%1;}"
            "QPushButton:hover{text-decoration:underline;}")
            .arg(out ? QStringLiteral("#F0ECFA") : QStringLiteral("#F3F1F8")));
        bubble->setMinimumWidth(220);
        bl->addWidget(geo);
        const QString url = msg.content;
        connect(geo, &QPushButton::clicked, this, [url]() {
            QString u = url;
            if (u.startsWith(QStringLiteral("geo:"))) {
                const QString c = u.mid(4).split('?').first();
                u = QStringLiteral("https://yandex.ru/maps/?text=") + c;
            }
            QDesktopServices::openUrl(QUrl(u));
        });
    } else if (msg.messageType == QStringLiteral("video") ||
               msg.messageType == QStringLiteral("video_note")) {
        // Видео как в Telegram: плеер в баббле; играет сразу, пока файл ещё
        // качается (стриминг через локальный Range-прокси с токеном), параллельно
        // докачивается в кэш. Видео-заметки — круглые, с кольцом прогресса.
        const bool isNote = msg.messageType == QStringLiteral("video_note");
        auto* video = new VideoMessageWidget(api_, msg.filePath, msg.fileName,
                                             msg.fileSize, isNote, bubble);
        if (!isNote) bubble->setMinimumWidth(340);
        bl->addWidget(video);
    } else if (msg.messageType == QStringLiteral("image") || msg.messageType == QStringLiteral("photo")) {
        // Фото 1:1 с .message-image веба: max-height 400px, радиус 8px,
        // клик — просмотр на весь экран. Кэш — мгновенный показ.
        auto* img = new QLabel(bubble);
        img->setAlignment(Qt::AlignCenter);
        img->setMinimumSize(180, 120);
        img->setStyleSheet(QStringLiteral("background:rgba(255,255,255,0.05);border-radius:10px;color:#ACA6BD;"));
        img->setText(QStringLiteral("Фото…"));
        img->setCursor(Qt::PointingHandCursor);
        img->installEventFilter(this);   // клик → просмотр на весь экран
        const QString path = msg.filePath;
        img->setProperty("filePath", path);
        bubble->setMinimumWidth(220);
        bl->addWidget(img);
        // Контекст-меню фото: копировать / сохранить / переслать / все фото чата.
        img->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(img, &QWidget::customContextMenuRequested, this, [this, img](const QPoint& p) {
            showMediaMenu(img, img->property("filePath").toString(), QStringLiteral("image"), p);
        });
        if (!path.isEmpty()) {
            if (!path.startsWith(QStringLiteral("/files"))) {
                QFile lf(path);   // локальный (только что отправленный)
                if (lf.open(QIODevice::ReadOnly)) setBubbleImage(img, lf.readAll());
            } else {
                // Мгновенно из локального зашифрованного кэша; с сервера — только
                // при первом обращении к файлу.
                QByteArray imgBytes;
                if (FileCache::instance().lookup(path, &imgBytes)) {
                    setBubbleImage(img, imgBytes);
                } else {
                    pendingImage_.insert(path, img);
                    api_->fetchFile(path);
                }
            }
        }
    } else if (msg.messageType == QStringLiteral("file")) {
        // Файл как в Telegram: кольцо загрузки вокруг иконки (скачать/отменить/
        // открыть), имя, размер, проценты. Сохраняется в «Загрузки/Xipher Desktop».
        const QString name = msg.fileName.isEmpty() ? QStringLiteral("файл") : msg.fileName;
        const QColor fclr = out ? QColor(0xF0, 0xEC, 0xFA) : QColor(0xF3, 0xF1, 0xF8);

        auto* rowWrap = new QWidget(bubble);
        auto* wrap = new QHBoxLayout(rowWrap);
        wrap->setContentsMargins(0, 0, 0, 0);
        wrap->setSpacing(12);

        auto* ring = new TransferRing(48, rowWrap);
        wrap->addWidget(ring, 0, Qt::AlignVCenter);

        auto* textCol = new QVBoxLayout();
        textCol->setSpacing(2);
        auto* nmL = new QLabel(name, rowWrap);
        nmL->setWordWrap(false);
        nmL->setStyleSheet(QString("border:none;background:transparent;font-size:14px;font-weight:600;color:%1;")
                               .arg(fclr.name()));
        auto* stL = new QLabel(humanSize(msg.fileSize), rowWrap);
        stL->setStyleSheet(QStringLiteral("border:none;background:transparent;font-size:12px;color:#ACA6BD;"));
        textCol->addWidget(nmL);
        textCol->addWidget(stL);
        wrap->addLayout(textCol, 1);
        bubble->setMinimumWidth(260);
        bl->addWidget(rowWrap);

        // Состояние загрузки конкретного файла (по пути).
        const QString fpath = msg.filePath;
        const QString savePath = QDir(downloadsDir()).filePath(name);
        ring->setState(QFile::exists(savePath) ? TransferRing::State::Done
                                               : TransferRing::State::Idle);
        auto startDownload = [this, ring, stL, fpath, savePath, name, size = msg.fileSize]() {
            ring->setState(TransferRing::State::Loading);
            stL->setText(QStringLiteral("0%  ·  ") + humanSize(size));
            DownloadCenter::instance().start(fpath, name, size);
            auto progConn = std::make_shared<QMetaObject::Connection>();
            *progConn = connect(api_, &ApiClient::fileProgress, this,
                [this, ring, stL, fpath, savePath, progConn, size](const QString& p, qint64 rec, qint64 tot) {
                    if (p != fpath) return;
                    if (tot <= 0) {                       // отмена/ошибка
                        QObject::disconnect(*progConn);
                        ring->setState(TransferRing::State::Idle);
                        stL->setText(humanSize(size));
                        return;
                    }
                    const int pct = int(rec * 100 / tot);
                    ring->setProgress(pct);
                    stL->setText(QStringLiteral("%1%  ·  %2 / %3")
                                     .arg(pct).arg(humanSize(rec), humanSize(tot)));
                    DownloadCenter::instance().progress(fpath, rec, tot);
                    if (rec >= tot && tot > 0) QObject::disconnect(*progConn);
                });
            auto doneConn = std::make_shared<QMetaObject::Connection>();
            *doneConn = connect(api_, &ApiClient::fileFetched, this,
                [this, doneConn, fpath, savePath, ring, stL](const QString& p, const QByteArray& bytes) {
                    if (p != fpath) return;
                    QObject::disconnect(*doneConn);
                    if (bytes.isEmpty()) return;
                    QFile out(savePath);
                    if (out.open(QIODevice::WriteOnly)) {
                        out.write(bytes);
                        out.close();
                    }
                    ring->setState(TransferRing::State::Done);
                    ring->setProgress(100);
                    stL->setText(humanSize(bytes.size()));
                    DownloadCenter::instance().finish(fpath, savePath);
                    QDesktopServices::openUrl(QUrl::fromLocalFile(savePath));
                });
            // Большие файлы — параллельными чанками (умножает скорость),
            // маленькие — обычным стримингом.
            if (size > 2 * 1024 * 1024)
                api_->fetchFileParallel(fpath, size, 6);
            else
                api_->fetchFile(fpath);
        };
        connect(ring, &TransferRing::clicked, this,
                [this, ring, stL, fpath, savePath, startDownload]() {
            switch (ring->state()) {
                case TransferRing::State::Idle:
                    startDownload();
                    break;
                case TransferRing::State::Loading:
                    api_->cancelFetch(fpath);
                    DownloadCenter::instance().cancel(fpath);
                    break;
                case TransferRing::State::Done:
                    QDesktopServices::openUrl(QUrl::fromLocalFile(savePath));
                    break;
            }
        });
    } else if (QStringList big = emojiOnlyClusters(msg.content); !big.isEmpty()) {
        // Только эмодзи (1-3) → крупно и анимированно, без баббла (как в Telegram).
        bubble->setStyleSheet(QStringLiteral("background:transparent;"));
        const int sz = big.size() == 1 ? 96 : (big.size() == 2 ? 64 : 52);
        auto* erow = new QWidget(bubble);
        erow->setStyleSheet(QStringLiteral("background:transparent;"));
        auto* el = new QHBoxLayout(erow);
        el->setContentsMargins(0, 0, 0, 0);
        el->setSpacing(4);
        if (out) el->addStretch();
        for (const QString& e : big) {
            auto* a = new AnimatedEmojiLabel(AnimatedEmojiLabel::emojiToCodepoints(e), sz, true, erow);
            el->addWidget(a);
        }
        if (!out) el->addStretch();
        bl->addWidget(erow);
    } else {
        auto* text = new MessageTextLabel(formatMessageHtml(msg.content, highlightQuery_), bubble);
        text->setContextMenuPolicy(Qt::NoContextMenu);   // ПКМ → меню баббла, не дефолтное
        text->setStyleSheet(QString("color:%1;font-size:15px;")
                                .arg(out ? QStringLiteral("#F0ECFA") : QStringLiteral("#F3F1F8")));
        bl->addWidget(text);
    }

    // Реакции: чипы [эмодзи ×N] под содержимым; своя — фиолетовая.
    addReactionChips(bubble, bl, msg);

    // Inline-кнопки бота (reply_markup.inline_keyboard): callback / url / web_app.
    if (!msg.replyMarkup.isEmpty())
        addInlineKeyboard(bl, msg);

    QString metaText = msg.time;
    if (msg.edited) metaText += QStringLiteral(" · изм.");
    if (out) {
        const QString tick = (msg.status == QStringLiteral("read"))
            ? QStringLiteral("✓✓") : (msg.status == QStringLiteral("delivered")
            ? QStringLiteral("✓✓") : QStringLiteral("✓"));
        metaText += QStringLiteral("  ") + tick;
    }
    auto* metaRow = new QHBoxLayout();
    metaRow->setContentsMargins(0, 0, 0, 0);
    metaRow->setSpacing(6);
    metaRow->addStretch();

    // Исчезающее сообщение: иконка-часы + обратный отсчёт.
    if (msg.ttlSeconds > 0) {
        auto* flame = new QLabel(bubble);
        flame->setPixmap(Icons::pixmap(Icons::Clock, 12,
            out ? QColor(0xF0,0xEC,0xFA) : QColor(0xAC,0xA6,0xBD)));
        auto* ttlLbl = new QLabel(bubble);
        ttlLbl->setStyleSheet(QString("color:%1;font-size:11px;font-weight:600;")
            .arg(out ? QStringLiteral("rgba(240,236,250,0.8)") : QStringLiteral("#ACA6BD")));

        const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + qint64(msg.ttlSeconds) * 1000;
        auto fmt = [](qint64 sec) -> QString {
            if (sec >= 3600) return QStringLiteral("%1ч").arg(sec / 3600);
            if (sec >= 60)   return QStringLiteral("%1м").arg(sec / 60);
            return QStringLiteral("%1с").arg(sec);
        };
        ttlLbl->setText(fmt(msg.ttlSeconds));

        auto* tick = new QTimer(ttlLbl);
        tick->setInterval(1000);
        QPointer<QWidget> rowGuard(row);
        connect(tick, &QTimer::timeout, this, [this, ttlLbl, deadline, fmt, rowGuard]() {
            const qint64 left = (deadline - QDateTime::currentMSecsSinceEpoch()) / 1000;
            if (left <= 0) {
                if (rowGuard) { rowGuard->deleteLater(); }   // сообщение «сгорело» (локально)
                return;
            }
            ttlLbl->setText(fmt(left));
        });
        tick->start();

        metaRow->addWidget(flame);
        metaRow->addWidget(ttlLbl);
    }

    auto* meta = new QLabel(metaText, bubble);
    meta->setStyleSheet(QString("color:%1;font-size:11px;")
        .arg(out ? QStringLiteral("rgba(240,236,250,0.65)") : QStringLiteral("#726C82")));
    metaRow->addWidget(meta);
    bl->addLayout(metaRow);

    if (out) { rl->addStretch(); rl->addWidget(bubble); }
    else     { rl->addWidget(bubble); rl->addStretch(); }

    if (prepend)
        msgLayout_->insertWidget(1, row);   // после stretch, ПЕРЕД существующими
    else
        msgLayout_->addWidget(row);         // после стартового stretch → прижато к низу

    // Появление нового сообщения — плавное (fade), как в Telegram/Discord.
    // Только для ЖИВЫХ добавлений: на начальном рендере 50 бабблов анимация
    // = лишние эффекты и лаг.
    if (animate && isVisible()) {
        auto* eff = new QGraphicsOpacityEffect(row);
        eff->setOpacity(0.0);
        row->setGraphicsEffect(eff);
        auto* a = new QPropertyAnimation(eff, "opacity", row);
        a->setDuration(160);
        a->setStartValue(0.0);
        a->setEndValue(1.0);
        a->setEasingCurve(QEasingCurve::OutCubic);
        a->start(QAbstractAnimation::DeleteWhenStopped);
    }
    ++bubbleCount_;
    if (greeting_ && greeting_->isVisible()) greeting_->hide();
}

void ChatPage::scrollToBottom() {
    stickBottom_ = true;
    auto* sb = msgScroll_->verticalScrollBar();
    programmaticScroll_ = true;
    // Далеко от низа — плавно, как в Telegram; рядом — мгновенно.
    if (sb->maximum() - sb->value() > 700 && sb->maximum() > 0)
        smoothScrollTo(sb->maximum());
    else
        sb->setValue(sb->maximum());
    programmaticScroll_ = false;
}


// ── Реакции (1:1 с message-reactions веба) ───────────────────────────────────

ChatMessage* ChatPage::findMessage(const QString& id) {
    for (ChatMessage& m : currentMessages_)
        if (m.id == id) return &m;
    return nullptr;
}

// Чипы [эмодзи ×N] под содержимым баббла. Своя реакция — фиолетовая.
void ChatPage::addReactionChips(QWidget* bubble, QVBoxLayout* bl, const ChatMessage& msg) {
    if (msg.reactions.isEmpty()) return;
    auto* row = new QWidget(bubble);
    row->setObjectName(QStringLiteral("reactionRow"));
    auto* lay = new QHBoxLayout(row);
    lay->setContentsMargins(2, 2, 2, 0);
    lay->setSpacing(4);
    const QString mid = msg.id;
    for (const Reaction& r : msg.reactions) {
        auto* chip = new QPushButton(row);
        chip->setObjectName(QStringLiteral("reactionChip"));
        chip->setCursor(Qt::PointingHandCursor);
        chip->setText(QStringLiteral("%1  %2").arg(r.emoji).arg(r.count));
        chip->setProperty("mine", r.mine);
        chip->setStyleSheet(QStringLiteral(
            "QPushButton{border:1px solid %1;background:%2;border-radius:11px;"
            "padding:2px 10px;color:%3;font-size:13px;}"
            "QPushButton:hover{background:%4;}")
            .arg(r.mine ? QStringLiteral("#8B5CF6") : QStringLiteral("rgba(255,255,255,0.10)"),
                 r.mine ? QStringLiteral("rgba(139,92,246,0.22)") : QStringLiteral("#221F2C"),
                 QStringLiteral("#F3F1F8"),
                 r.mine ? QStringLiteral("rgba(139,92,246,0.34)") : QStringLiteral("#2B2737")));
        const QString emoji = r.emoji;
        connect(chip, &QPushButton::clicked, this, [this, mid, emoji]() {
            toggleReaction(mid, emoji);
        });
        lay->addWidget(chip);
    }
    lay->addStretch(1);
    bl->addWidget(row);
}

// Точечное обновление чипов одного сообщения (без перерисовки всего чата).
void ChatPage::refreshReactionChips(const QString& messageId) {
    QWidget* target = nullptr;
    for (QWidget* w : msgContainer_->findChildren<QWidget*>()) {
        if (w->property("msgId").toString() == messageId) { target = w; break; }
    }
    const ChatMessage* m = findMessage(messageId);
    if (!target || !m) return;
    if (auto* old = target->findChild<QWidget*>(QStringLiteral("reactionRow"))) {
        if (auto* lay = qobject_cast<QVBoxLayout*>(target->layout())) {
            const int at = lay->indexOf(old);
            if (at >= 0) delete lay->takeAt(at);
        }
        old->deleteLater();
    }
    if (auto* lay = qobject_cast<QVBoxLayout*>(target->layout()))
        addReactionChips(target, lay, *m);
}

// Тумбл: оптимистично меняем модель+UI, эхо WS reaction_update сверит.
void ChatPage::toggleReaction(const QString& messageId, const QString& emoji) {
    ChatMessage* m = findMessage(messageId);
    if (!m || messageId.isEmpty()) return;
    const QString ctx = currentKind_ == ChatKind::Group ? QStringLiteral("group")
                                                      : QStringLiteral("chat");
    bool found = false;
    for (Reaction& r : m->reactions) {
        if (r.emoji != emoji) continue;
        found = true;
        if (r.mine) { r.mine = false; if (--r.count <= 0) m->reactions.removeOne(r); }
        else        { r.mine = true;  ++r.count; }
        break;
    }
    if (!found) m->reactions.append(Reaction{emoji, 1, true});
    const bool nowMine = [&]{ for (const Reaction& r : m->reactions) if (r.emoji==emoji) return r.mine; return false; }();
    refreshReactionChips(messageId);
    if (nowMine) api_->addMessageReaction(messageId, emoji, ctx);
    else         api_->removeMessageReaction(messageId, emoji, ctx);
}


// ── Правка своего сообщения ─────────────────────────────────────────────────

void ChatPage::startEditing(const QString& id, const QString& text) {
    if (id.isEmpty() || id.startsWith(QStringLiteral("tmp_"))) return;
    cancelEditing();
    clearReplyTo();
    editingId_ = id;
    if (composer_) composer_->setPlainText(text);
    if (replyBarText_)
        replyBarText_->setText(QStringLiteral("Редактирование: %1")
            .arg(elide(text, replyBarText_->font(), 300)));
    if (replyBar_) replyBar_->setVisible(true);
    if (composer_) composer_->setFocus();
}

void ChatPage::cancelEditing() {
    if (editingId_.isEmpty()) return;
    editingId_.clear();
    if (replyBar_) replyBar_->setVisible(false);
    if (composer_) composer_->clear();
}

// Перерисовка чата с сохранением позиции прокрутки (для правок содержимого).
void ChatPage::rerenderPreservingScroll() {
    QScrollBar* sb = msgScroll_->verticalScrollBar();
    const int v = sb->value(), mx = sb->maximum();
    const bool stick = stickBottom_;
    renderMessages(QString());
    for (int i = 0; i < 8; ++i) QCoreApplication::processEvents();
    sb->setValue(mx > 0 ? int(double(v) / mx * sb->maximum()) : 0);
    stickBottom_ = stick;
}

// ── Закреплённое сообщение ──────────────────────────────────────────────────

void ChatPage::setPinnedMessage(const QString& id, const QString& snippet) {
    pinnedMsgId_ = id;
    if (!pinnedBar_) {
        pinnedBar_ = new QWidget(this);
        pinnedBar_->setObjectName(QStringLiteral("pinnedBar"));
        pinnedBar_->setStyleSheet(QStringLiteral(
            "QWidget#pinnedBar{background:#1A1822;border-bottom:1px solid #221F2C;}"));
        pinnedBar_->setAttribute(Qt::WA_StyledBackground, true);
        auto* pl = new QHBoxLayout(pinnedBar_);
        pl->setContentsMargins(14, 6, 10, 6);
        pl->setSpacing(10);
        auto* pinIc = new QLabel(QStringLiteral("📌"), pinnedBar_);
        pinIc->setStyleSheet(QStringLiteral("font-size:14px;background:transparent;"));
        pinnedText_ = new QLabel(pinnedBar_);
        pinnedText_->setStyleSheet(QStringLiteral(
            "color:#ACA6BD;font-size:13px;background:transparent;"));
        auto* unpin = new QPushButton(QStringLiteral("✕"), pinnedBar_);
        unpin->setCursor(Qt::PointingHandCursor);
        unpin->setFixedSize(22, 22);
        unpin->setStyleSheet(QStringLiteral(
            "QPushButton{border:none;border-radius:11px;color:#726C82;font-size:12px;}"
            "QPushButton:hover{background:#221F2C;color:#F3F1F8;}"));
        connect(unpin, &QPushButton::clicked, this, [this]() {
            if (!pinnedMsgId_.isEmpty())
                api_->pinMessage(pinnedMsgId_, currentKind_, currentPeerId_, false);
            clearPinnedMessage();
        });
        // Клик по плашке — прыжок к сообщению.
        pinnedBar_->installEventFilter(new SuperSearchClickFilter([this]() {
            if (!pinnedMsgId_.isEmpty()) jumpToMessage(pinnedMsgId_);
        }, pinnedBar_));
        pl->addWidget(pinIc);
        pl->addWidget(pinnedText_, 1);
        pl->addWidget(unpin);
        // Плашка ставится над областью сообщений: первым виджетом convLayout?
        if (auto* outer = qobject_cast<QVBoxLayout*>(msgScroll_->parentWidget()->layout())) {
            const int idx = outer->indexOf(msgScroll_);
            if (idx >= 0) outer->insertWidget(idx, pinnedBar_);
            else outer->insertWidget(0, pinnedBar_);
        }
    }
    if (pinnedText_)
        pinnedText_->setText(elide(snippet, pinnedText_->font(), 420));
    pinnedBar_->setVisible(true);
}

void ChatPage::clearPinnedMessage() {
    pinnedMsgId_.clear();
    if (pinnedBar_) pinnedBar_->hide();
}


// «Печатает…»: подменяет статус в шапке и гасится через 4 с.
void ChatPage::showTyping(const QString& chatId, bool on) {
    if (chatId != currentPeerId_ || !peerStatus_) return;
    if (peerStatusBase_.isEmpty()) peerStatusBase_ = peerStatus_->text();
    if (on) {
        peerStatus_->setText(QStringLiteral("печатает…"));
        typingTimer_->start();
    } else {
        peerStatus_->setText(peerStatusBase_);
        typingTimer_->stop();
    }
}

// Черновики: текст композера живёт per-chat (Prefs), восстанавливается
// при переключении, уходит после отправки.
void ChatPage::saveDraft() {
    if (draftKey_.isEmpty() || !composer_) return;
    Prefs::setStr(draftKey_, composer_->toPlainText());
}

void ChatPage::restoreDraft() {
    if (!composer_) return;
    composer_->blockSignals(true);
    composer_->clear();
    const QString t = Prefs::getStr(draftKey_);
    if (!t.isEmpty()) composer_->setPlainText(t);
    composer_->blockSignals(false);
}

void ChatPage::showMessageMenu(QWidget* bubble, const QPoint& pos) {
    const QString id     = bubble->property("msgId").toString();
    const QString text   = bubble->property("msgText").toString();
    const bool    sent   = bubble->property("msgSent").toBool();
    const QString author = bubble->property("msgAuthor").toString();

    const bool plain = !text.isEmpty() && !text.startsWith(QStringLiteral("[["));
    const bool isSavedChat = [&]() {
        const int idx = indexOfChat(currentPeerId_);
        return idx >= 0 && chats_[idx].isSaved;
    }();
    QMenu menu(this);
    // Быстрые реакции (Telegram-class): сегмент из 8 эмодзи над пунктами меню.
    if (!id.isEmpty() && currentKind_ != ChatKind::Channel) {
        auto* bar = new QWidget(&menu);
        auto* bl2 = new QHBoxLayout(bar);
        bl2->setContentsMargins(10, 6, 10, 6);
        bl2->setSpacing(2);
        const QStringList quick = {QString::fromUtf8("\U0001F44D"),
                                   QString::fromUtf8("\u2764\uFE0F"),
                                   QString::fromUtf8("\U0001F602"),
                                   QString::fromUtf8("\U0001F62E"),
                                   QString::fromUtf8("\U0001F622"),
                                   QString::fromUtf8("\U0001F525"),
                                   QString::fromUtf8("\U0001F44F"),
                                   QString::fromUtf8("\U0001F389")};
        for (const QString& e : quick) {
            auto* b = new QPushButton(e, bar);
            b->setFlat(true);
            b->setCursor(Qt::PointingHandCursor);
            b->setStyleSheet(QStringLiteral(
                "QPushButton{border:none;border-radius:8px;font-size:18px;padding:4px 6px;}"
                "QPushButton:hover{background:rgba(139,92,246,0.25);}"));
            const QString emoji = e, mid = id;
            connect(b, &QPushButton::clicked, this, [this, mid, emoji, &menu]() {
                toggleReaction(mid, emoji);
                menu.close();
            });
            bl2->addWidget(b);
        }
        auto* wa = new QWidgetAction(&menu);
        wa->setDefaultWidget(bar);
        menu.addAction(wa);
        menu.addSeparator();
    }
    QAction* reply = menu.addAction(QStringLiteral("Ответить"));
    // Пересылка (MSG-02): любой тип — текст и медиа (по file_path, без перезалива).
    QAction* forward = menu.addAction(QStringLiteral("Переслать"));
    QAction* fav = nullptr;
    if (plain && !isSavedChat)   // «В избранное» — как пересылка в «Избранные» (1 клик)
        fav = menu.addAction(QStringLiteral("⭐  В избранное"));
    QAction* copy = plain ? menu.addAction(QStringLiteral("Копировать")) : nullptr;
    QAction* pin = nullptr;
    if ((currentKind_ == ChatKind::Group || currentKind_ == ChatKind::Channel)
        && !id.isEmpty() && !id.startsWith(QStringLiteral("tmp_")))
        pin = menu.addAction(QStringLiteral("Закрепить"));
    QAction* edit = nullptr;
    if (sent && plain && !id.isEmpty() && !id.startsWith(QStringLiteral("tmp_")))
        edit = menu.addAction(QStringLiteral("Изменить"));
    QAction* del = nullptr;
    if (sent && !id.isEmpty() && !id.startsWith(QStringLiteral("tmp_"))) {
        menu.addSeparator();
        del = menu.addAction(QStringLiteral("Удалить"));
    }
    QAction* ch = menu.exec(pos);
    if (!ch) return;
    if (ch == reply)         setReplyTo(id, author, text);
    else if (edit && ch == edit) startEditing(id, text);
    else if (ch == copy)     QApplication::clipboard()->setText(text);
    else if (ch == del)      api_->deleteMessage(id, currentKind_, currentPeerId_);
    else if (ch == forward) {
        if (ChatMessage* mm = findMessage(id)) forwardMessageFull(*mm);
        else forwardMessage(text);
    }
    else if (fav && ch == fav) saveToSaved(text);
    else if (pin && ch == pin) api_->pinMessage(id, currentKind_, currentPeerId_, true);
}

// «В избранное» (Saved Messages): копия сообщения в один клик улетает
// в чат «Избранные» — как пересылка в Telegram, без диалога выбора.
void ChatPage::saveToSaved(const QString& text) {
    QString savedId = Session::instance().userId;   // у «Избранных» id = свой
    for (const Chat& c : chats_)
        if (c.isSaved) { savedId = c.id; break; }
    api_->sendMessage(savedId, text, QStringLiteral("fav_%1").arg(++tempCounter_));
    bumpChat(savedId, text, QTime::currentTime().toString(QStringLiteral("HH:mm")), false);
}

void ChatPage::forwardMessage(const QString& text) {
    auto* picker = new ChatPickerDialog(chats_, QStringLiteral("Переслать в…"), window());
    connect(picker, &ChatPickerDialog::picked, this, [this, text](const Chat& c) {
        const QString tempId = QStringLiteral("tmp_%1").arg(++tempCounter_);
        if (c.kind == ChatKind::Group)        api_->sendGroupMessage(c.id, text, tempId);
        else if (c.kind == ChatKind::Channel) api_->sendChannelMessage(c.id, text, tempId);
        else                                  api_->sendMessage(c.id, text, tempId);
        openChat(c);   // переходим в чат назначения
    });
    picker->showAnimated();
}

// Полная пересылка (MSG-02): текст + аттачи одним сообщением; аттач идёт по
// file_path (байты уже на сервере — без перезалива). Галка «без имени автора»
// убирает префикс «Переслано от …» (как hide-sender в вебе).
void ChatPage::forwardMessageFull(const ChatMessage& msg) {
    auto* picker = new ChatPickerDialog(chats_, QStringLiteral("Переслать в…"), window());
    auto* hideBox = new QCheckBox(QStringLiteral("Не указывать автора"), picker->card());
    hideBox->setCursor(Qt::PointingHandCursor);
    hideBox->setStyleSheet(QStringLiteral(
        "QCheckBox{color:#ACA6BD;font-size:13px;padding:6px 14px 10px 14px;}"
        "QCheckBox::indicator{width:16px;height:16px;border-radius:4px;"
        "border:1px solid rgba(255,255,255,0.25);background:#131218;}"
        "QCheckBox::indicator:checked{background:#8B5CF6;border-color:#8B5CF6;}"));
    picker->cardLayout()->addWidget(hideBox);
    connect(picker, &ChatPickerDialog::picked, this, [this, msg, hideBox](const Chat& c) {
        const bool hide = hideBox->isChecked();
        const QString author = !msg.senderName.isEmpty() ? msg.senderName
                               : msg.sent ? Session::instance().username
                                          : currentPeerName_;
        // Сервер требует непустой content: у чистого медиа подписью станет имя файла.
        QString body = msg.content;
        if (body.isEmpty() && !msg.fileName.isEmpty()) body = msg.fileName;
        if (body.isEmpty() && !msg.filePath.isEmpty()) body = QStringLiteral("[медиа]");
        const QString content = hide ? body
            : QStringLiteral("Переслано от %1:\n%2").arg(author, body);
        const QString tempId = QStringLiteral("fw_%1").arg(++tempCounter_);
        if (!msg.filePath.isEmpty()) {
            const QString type = msg.messageType == QStringLiteral("image")
                               ? QStringLiteral("image") : msg.messageType;
            if (c.kind == ChatKind::Group)
                api_->sendGroupFile(c.id, msg.filePath, msg.fileName, msg.fileSize, content, tempId, type);
            else if (c.kind == ChatKind::Channel)
                api_->sendChannelFile(c.id, msg.filePath, msg.fileName, msg.fileSize, content, tempId, type);
            else
                api_->sendFile(c.id, msg.filePath, msg.fileName, msg.fileSize, content, tempId, type);
        } else {
            if (c.kind == ChatKind::Group)        api_->sendGroupMessage(c.id, content, tempId);
            else if (c.kind == ChatKind::Channel) api_->sendChannelMessage(c.id, content, tempId);
            else                                  api_->sendMessage(c.id, content, tempId);
        }
        openChat(c);   // переходим в чат назначения (эхо придёт WS/ack)
    });
    picker->showAnimated();
}

void ChatPage::setReplyTo(const QString& id, const QString& author, const QString& text) {
    if (id.isEmpty() || id.startsWith(QStringLiteral("tmp_"))) return;
    replyToId_ = id;
    replyToName_ = author;
    replyToText_ = text;
    if (replyBarText_)
        replyBarText_->setText(QStringLiteral("Ответ %1: %2")
            .arg(author, elide(text, replyBarText_->font(), 300)));
    if (replyBar_) replyBar_->setVisible(true);
    if (composer_) composer_->setFocus();
}

void ChatPage::clearReplyTo() {
    replyToId_.clear(); replyToName_.clear(); replyToText_.clear();
    if (replyBar_) replyBar_->setVisible(false);
}

void ChatPage::reloadCurrentMessages() {
    if (!currentTopicId_.isEmpty())             { api_->getTopicMessages(currentTopicId_); return; }
    if (currentPeerId_.isEmpty()) return;
    if (currentKind_ == ChatKind::Group)        api_->getGroupMessages(currentPeerId_);
    else if (currentKind_ == ChatKind::Channel) api_->getChannelMessages(currentPeerId_);
    else                                        api_->getMessages(currentPeerId_);
}

void ChatPage::onSendClicked() {
    const QString text = composer_->toPlainText().trimmed();
    const bool hasStaged = !stagedFiles_.isEmpty();
    if ((text.isEmpty() && !hasStaged) || currentPeerId_.isEmpty()) {
        if (hasStaged && currentPeerId_.isEmpty())
            QMessageBox::information(this, QStringLiteral("Отправка"),
                                     QStringLiteral("Выберите чат для отправки вложений"));
        return;
    }

    // Режим правки: уходит edit-message, баббл обновляется на месте.
    // (пустой текст в правке ничем не оправдан — ждём непустой)
    if (!editingId_.isEmpty() && !text.isEmpty()) {
        const QString eid = editingId_;
        api_->editMessage(eid, text, currentKind_);
        if (ChatMessage* m = findMessage(eid)) {
            m->content = text;
            m->edited = true;
            rerenderPreservingScroll();
        }
        cancelEditing();
        return;
    }

    // Текст уходит первым сообщением; только аттачи — без текстового баббла.
    if (!text.isEmpty()) {
        const QString replyTo = replyToId_;
        const QString tempId = QStringLiteral("tmp_%1").arg(++tempCounter_);
        ChatMessage m;
        m.content = text;
        m.sent = true;
        m.status = QStringLiteral("sent");
        m.time = QTime::currentTime().toString(QStringLiteral("HH:mm"));
        m.id = tempId;
        m.ttlSeconds = disappearTtl_;
        m.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
        if (!replyTo.isEmpty()) { m.replyAuthor = replyToName_; m.replySnippet = replyToText_; }
        shownIds_.insert(tempId);

        currentMessages_.append(m);
        addBubble(m);
        scrollToBottom();
        cacheCurrent();   // отправленное сразу в локальном кэше
        composer_->clear();
        clearReplyTo();

        if (!currentTopicId_.isEmpty())             api_->sendTopicMessage(currentTopicId_, text, tempId, replyTo);
        else if (currentKind_ == ChatKind::Group)   api_->sendGroupMessage(currentPeerId_, text, tempId, replyTo);
        else if (currentKind_ == ChatKind::Channel) api_->sendChannelMessage(currentPeerId_, text, tempId, replyTo);
        else                                        api_->sendMessage(currentPeerId_, text, tempId, disappearTtl_, replyTo);
        if (currentTopicId_.isEmpty())
            bumpChat(currentPeerId_, text, m.time, /*incrementUnread*/ false);
    }

    // Отложенные аттачи (MLT-06): уходят следом за текстом, каждый своим
    // сообщением (как отправка файлов по одному в вебе).
    if (hasStaged) {
        const QList<StagedAttachment> toSend = stagedFiles_;
        clearStagedFiles();
        for (const StagedAttachment& a : toSend) sendLocalFile(a.path, a.isImage);
    }
}

void ChatPage::onMessageSent(const ChatMessage& msg, const QString& receiverId, const QString& tempId) {
    Q_UNUSED(receiverId);
    Q_UNUSED(tempId);
    if (!msg.id.isEmpty()) shownIds_.insert(msg.id);   // чтобы WS-эхо не задублировало
}



// ── Медиа: байты файла (кэш → локальный файл → пусто) ────────────────────────
QByteArray ChatPage::mediaBytes(const QString& filePath) const {
    QByteArray bytes;
    if (FileCache::instance().lookup(filePath, &bytes) && !bytes.isEmpty()) return bytes;
    if (!filePath.startsWith(QStringLiteral("/files"))) {
        QFile f(filePath);
        if (f.open(QIODevice::ReadOnly)) return f.readAll();
    }
    return {};
}

// ── Контекст-меню фото/голосового (сохранить / копировать / переслать / все) ──
void ChatPage::showMediaMenu(QWidget* src, const QString& filePath,
                             const QString& kind, const QPoint& pos) {
    QMenu menu(src);
    QAction* copyAct  = (kind == QStringLiteral("image"))
        ? menu.addAction(QStringLiteral("Копировать")) : nullptr;
    QAction* saveAct  = menu.addAction(QStringLiteral("Сохранить как…"));
    QAction* fwdAct   = menu.addAction(QStringLiteral("Переслать…"));
    QAction* allAct   = (kind == QStringLiteral("image"))
        ? menu.addAction(QStringLiteral("Все фото чата")) : nullptr;
    QAction* chosen = menu.exec(src->mapToGlobal(pos));
    if (!chosen) return;

    const QByteArray bytes = mediaBytes(filePath);

    if (chosen == copyAct) {
        QPixmap pm;
        if (pm.loadFromData(bytes)) QApplication::clipboard()->setPixmap(pm);
        return;
    }
    if (chosen == saveAct) {
        QString name = QFileInfo(filePath).fileName();
        if (name.isEmpty()) name = kind == QStringLiteral("voice")
            ? QStringLiteral("voice.webm") : QStringLiteral("photo.png");
        const QString target = QDir(downloadsDir()).filePath(name);
        const QString dest = QFileDialog::getSaveFileName(this, QStringLiteral("Сохранить"), target);
        if (dest.isEmpty()) return;
        if (bytes.isEmpty()) {                       // ещё не скачано → скачаем и сохраним
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = connect(api_, &ApiClient::fileFetched, this,
                [conn, dest](const QString& p, const QByteArray& b) {
                    if (p.isEmpty() || b.isEmpty()) return;
                    QObject::disconnect(*conn);
                    QFile out(dest);
                    if (out.open(QIODevice::WriteOnly)) out.write(b);
                });
            api_->fetchFile(filePath);
        } else {
            QFile out(dest);
            if (out.open(QIODevice::WriteOnly)) out.write(bytes);
        }
        return;
    }
    if (chosen == fwdAct) {
        // Есть сообщение с этим filePath → полный форвард (MSG-02: аттач по
        // file_path + галка «без автора»); чистые temp-файлы — старый путь байтами.
        for (const ChatMessage& m : currentMessages_) {
            if (m.filePath == filePath && !m.filePath.isEmpty()) {
                forwardMessageFull(m);
                return;
            }
        }
        // Пересылка = повторная отправка того же медиа выбранным чатам.
        auto* picker = new ChatPickerDialog(chats_, QStringLiteral("Переслать в…"), window());
        connect(picker, &ChatPickerDialog::picked, this,
                [this, filePath, bytes, kind](const Chat& c) {
            if (kind == QStringLiteral("voice")) {
                const QString tmp = QDir::temp().filePath(
                    QStringLiteral("fwd_%1").arg(QFileInfo(filePath).fileName()));
                QFile f(tmp);
                if (!f.open(QIODevice::WriteOnly)) return;
                f.write(bytes); f.close();
                api_->sendVoice(c.id, tmp, QFileInfo(filePath).fileName(),
                                bytes.size(), QString(),
                                QStringLiteral("fw_%1").arg(++tempCounter_));
            } else {
                sendPhotoBytesTo(c.id, bytes,
                                 QFileInfo(filePath).fileName().isEmpty()
                                     ? QStringLiteral("photo.png")
                                     : QFileInfo(filePath).fileName());
            }
        }, Qt::UniqueConnection);
        picker->showAnimated();
        return;
    }
    if (chosen == allAct) {
        // Галерея: все фото текущего чата в хронологическом порядке.
        QStringList paths;
        int startIdx = 0;
        for (const ChatMessage& m : currentMessages_) {
            if ((m.messageType == QStringLiteral("image")
                 || m.messageType == QStringLiteral("photo")) && !m.filePath.isEmpty()) {
                if (m.filePath == filePath) startIdx = paths.size();
                paths.append(m.filePath);
            }
        }
        ImageViewer::showGallery(window(), paths, startIdx,
            [this](const QString& p) { return mediaBytes(p); },
            [this](const QString& p) { api_->fetchFile(p); });
    }
}


void ChatPage::onWsMessage(const QString& peerId, const ChatMessage& msgIn, const QString& tempId) {
    ChatMessage msg = msgIn;
    msg.sent = (msg.senderId == Session::instance().userId);

    const bool dupTemp = !tempId.isEmpty() && shownIds_.contains(tempId);
    const bool dupId   = !msg.id.isEmpty() && shownIds_.contains(msg.id);

    // Внутри темы форума root-сообщения группы не подмешиваем в открытую тему.
    if (peerId == currentPeerId_ && !dupTemp && !dupId && currentTopicId_.isEmpty()) {
        if (!msg.id.isEmpty()) shownIds_.insert(msg.id);
        if (!msg.content.startsWith(ChecklistProto::kUpdatePrefix)) {
            if (msg.createdAt.isEmpty()) msg.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
            currentMessages_.append(msg);
        }
        addBubble(msg);
        // Бот прислал новую reply-клавиатуру (или убрал) — применяем сразу.
        if (!msg.replyMarkup.isEmpty()) {
            if (msg.replyMarkup.contains(QStringLiteral("keyboard")))
                applyReplyKeyboard(msg.replyMarkup);
            else if (msg.replyMarkup.contains(QStringLiteral("remove_keyboard")))
                hideBotKeyboard();
        }
        // stickBottom_ уже держит низ (якорь на rangeChanged); если пользователь
        // читает историю — не дёргаем. Своё сообщение — всегда к низу.
        if (msg.sent) scrollToBottom();
        cacheCurrent();   // кэш живой: новое сообщение уже в локальной истории
    }

    bumpChat(peerId, msg.content, msg.time,
             /*incrementUnread*/ (!msg.sent && peerId != currentPeerId_));

    // Системное уведомление о входящем (не своём) сообщении.
    if (!msg.sent && !msg.content.startsWith(QStringLiteral("[["))
        && !msg.content.startsWith(ChecklistProto::kUpdatePrefix)) {
        const int idx = indexOfChat(peerId);
        const QString who = idx >= 0 ? chats_[idx].displayName : QStringLiteral("Сообщение");
        emit notify(who, chatPreview(msg.content));
    }
}

void ChatPage::bumpChat(const QString& peerId, const QString& lastText,
                        const QString& time, bool incrementUnread) {
    const int idx = indexOfChat(peerId);
    if (idx < 0) {
        api_->getChats();   // новый собеседник — перезагрузим список
        return;
    }
    chats_[idx].lastMessage = lastText;
    if (!time.isEmpty()) chats_[idx].time = time;
    if (incrementUnread) chats_[idx].unread += 1;
    // Полная пересборка списка на КАЖДОЕ сообщение (Active-чат = шторм) —
    // дебаунсим: данные уже в chats_, виджет обновится пачкой.
    if (!chatListDebounce_) {
        chatListDebounce_ = new QTimer(this);
        chatListDebounce_->setSingleShot(true);
        chatListDebounce_->setInterval(250);
        connect(chatListDebounce_, &QTimer::timeout, this, &ChatPage::rebuildChatList);
    }
    chatListDebounce_->start();
}

void ChatPage::onSearchChanged(const QString& text) {
    rebuildChatList();   // мгновенно фильтруем существующие чаты
    const QString q = text.trimmed();
    if (q.length() >= 2) {
        searchQuery_ = q;
        searchTimer_->start();   // дебаунс глобального поиска людей
        // Глобально: публичные каналы и группы из каталога (не только свои чаты).
        api_->publicDirectory(QStringLiteral("all"), q, 0);
    } else {
        searchQuery_.clear();
        bool dirty = false;
        if (!searchHits_.isEmpty()) { searchHits_.clear(); dirty = true; }
        if (!directoryHits_.isEmpty()) { directoryHits_.clear(); dirty = true; }
        if (dirty) rebuildChatList();
    }
}

static QString chatQSS() {
    const auto th = ThemePreset::current();
    return QStringLiteral(
        "#sidebar { background:%1; border-right:1px solid rgba(255,255,255,0.10); }"
        "#sideHeader, #convHeader { background:%1; border-bottom:1px solid rgba(255,255,255,0.10); }"
        "#peerHeader { border-radius:10px; }"
        "#peerHeader:hover { background:%2; }"
        "#hdrBtn { border:none; background:transparent; min-width:40px; min-height:40px; border-radius:20px; }"
        "#hdrBtn:hover { background:%2; }"
        "#searchBar { background:%1; border-bottom:1px solid rgba(255,255,255,0.10); }"
        "#msgSearch { background:%2; border:1px solid rgba(255,255,255,0.10); border-radius:12px;"
        "  min-height:38px; padding:0 14px; color:#F3F1F8; }"
        "#msgSearch:focus { border:1px solid %5; }"
        "#brandTitle { font-size:18px; font-weight:800; color:#F3F1F8; }"
        "#searchBox { background:%2; border:1px solid rgba(255,255,255,0.10); border-radius:12px;"
        "  min-height:38px; padding:0 14px; color:#F3F1F8; }"
        "#searchBox:focus { border:1px solid %5; }"
        "#catalogBtn { background:%2; border:1px solid rgba(255,255,255,0.10); border-radius:12px;"
        "  color:#BBA4FF; font-size:13px; font-weight:600; min-height:38px; padding:0 12px; }"
        "#catalogBtn:hover { border-color:%5; color:#F3F1F8; }"
        "#botKeyboardBar { background:transparent; }"
        "#botKeyboardBtn { background:%2; border:1px solid rgba(255,255,255,0.10); border-radius:12px;"
        "  min-height:38px; padding:0 14px; color:#BBA4FF; font-size:14px; text-align:center; }"
        "#botKeyboardBtn:hover { border-color:%5; color:#F3F1F8; background:rgba(%6,%7,%8,0.10); }"
        "#chatList { background:%1; border:none; outline:none; }"
        "#chatList::item { border:none; padding:0; }"
        "#chatList::item:hover { background:%2; border-left:3px solid %5; }"
        "#chatList::item:selected { background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "  stop:0 rgba(%6,%7,%8,0.16), stop:1 rgba(%6,%7,%8,0.06)); border-left:3px solid %5; }"
        "#msgArea { background:%4; border:none; }"
        "#msgArea > QWidget > QWidget { background:%4; }"
        "#msgArea QScrollBar:vertical { background:transparent; width:8px; margin:2px; }"
        "#msgArea QScrollBar::handle:vertical { background:rgba(255,255,255,0.12); border-radius:4px; min-height:36px; }"
        "#msgArea QScrollBar::handle:vertical:hover { background:rgba(255,255,255,0.22); }"
        "#msgArea QScrollBar::add-line:vertical, #msgArea QScrollBar::sub-line:vertical { height:0; }"
        "#msgArea QScrollBar::add-page:vertical, #msgArea QScrollBar::sub-page:vertical { background:transparent; }"
        "#chatList QScrollBar:vertical { background:transparent; width:8px; margin:2px; }"
        "#chatList QScrollBar::handle:vertical { background:rgba(255,255,255,0.10); border-radius:4px; min-height:36px; }"
        "#chatList QScrollBar::handle:vertical:hover { background:rgba(255,255,255,0.20); }"
        "#chatList QScrollBar::add-line:vertical, #chatList QScrollBar::sub-line:vertical { height:0; }"
        "#chatList QScrollBar::add-page:vertical, #chatList QScrollBar::sub-page:vertical { background:transparent; }"
        "#composerBar { background:%1; border-top:1px solid rgba(255,255,255,0.10); }"
        "#tgInputBar { background:%2; border:1px solid rgba(255,255,255,0.07); border-radius:22px; }"
        "#composer { background:transparent; border:none; color:#F3F1F8; font-size:15px; padding:0 4px; }"
        "#composer QScrollBar:vertical { background:transparent; width:7px; margin:4px 2px; }"
        "#composer QScrollBar::handle:vertical { background:rgba(255,255,255,0.18); border-radius:3px; }"
        "#composer QScrollBar::add-line:vertical, #composer QScrollBar::sub-line:vertical { height:0; }"
        "#replyBarText { color:#ACA6BD; font-size:13px; }"
        "#replyClose { background:transparent; border:none; color:#726C82; font-size:14px; }"
        "#replyClose:hover { color:#F3F1F8; }"
        "#replyBar { background:%1; border-top:1px solid rgba(255,255,255,0.06); }"
        "#stagedBar { background:%1; border-top:1px solid rgba(255,255,255,0.06); }"
        "#stagedChip { background:%3; border:1px solid rgba(255,255,255,0.10); border-radius:12px; }"
        "#dropOverlay { background:rgba(139,92,246,0.14); border:3px dashed rgba(139,92,246,0.65); }"
        "QSplitter::handle:horizontal { background:transparent; width:4px; }"
        "#thirdCol { background:%2; border-left:1px solid rgba(255,255,255,0.10); }"
        "#thirdColHeader { background:%2; border-bottom:1px solid rgba(255,255,255,0.10); }"
        "#tcTitle { color:#F3F1F8; font-size:15px; font-weight:700; }"
        "#tcBtn { background:transparent; border:none; border-radius:18px; color:#ACA6BD; font-size:15px; }"
        "#tcBtn:hover { background:%3; color:#F3F1F8; }"
        "#tcName { color:#F3F1F8; font-size:17px; font-weight:600; }"
        "#tcSub { color:#726C82; font-size:13px; }"
        "#tcAction { background:rgba(%6,%7,%8,0.16); border:none; border-radius:12px;"
        "  color:#F3F1F8; font-size:14px; font-weight:600; min-height:38px; }"
        "#tcAction:hover { background:rgba(%6,%7,%8,0.28); }"
        "#tcActionGhost { background:transparent; border:1px solid rgba(255,255,255,0.14);"
        "  border-radius:12px; color:#ACA6BD; font-size:14px; min-height:36px; }"
        "#tcActionGhost:hover { background:%3; color:#F3F1F8; }"
        "#tcMetaKey { color:#726C82; font-size:12px; }"
        "#tcMetaVal { color:#ACA6BD; font-size:13px; }"
        "#overlayScrim { background:rgba(0,0,0,140); }"
        "#sendBtn { min-width:36px; max-width:36px; min-height:36px; max-height:36px; border:none;"
        "  border-radius:18px; background:transparent; color:%5; padding:0; }"
        "#sendBtn:hover { background:rgba(%6,%7,%8,0.10); }"
        "#composerIcon, #micBtn { min-width:36px; max-width:36px; min-height:36px; max-height:36px;"
        "  border:none; border-radius:18px; background:transparent; color:#ACA6BD; padding:0; }"
        "#composerIcon:hover, #micBtn:hover { background:rgba(255,255,255,0.06); color:#F3F1F8; }"
        "QMenu { background:%2; border:1px solid rgba(255,255,255,0.12); border-radius:10px; color:#F3F1F8; }"
        "QMenu::item { padding:8px 18px; border-radius:6px; }"
        "QMenu::item:selected { background:rgba(%6,%7,%8,0.22); }"
        "#peerName { font-size:15px; font-weight:700; color:#F3F1F8; }"
        "#peerStatus { font-size:12px; color:#726C82; }"
        "#emptyHint { font-size:15px; color:#726C82; }"
        "#iconBtn { border:none; background:transparent; color:#ACA6BD; font-size:13px; }"
        "#chatsBody { background:%1; }"
        "#folderRail { background:%1; border-right:1px solid rgba(255,255,255,0.10); }"
        "#folderRailItem { background:transparent; border:none; border-radius:16px; padding:0; }"
        "#folderRailItem:hover { background:%2; }"
        "#folderRailItemActive { background:rgba(%6,%7,%8,0.14); border:none; border-radius:16px; padding:0; }"
        "#folderRailLabel { color:#ACA6BD; font-size:11px; }"
        "#folderRailItemActive #folderRailLabel { color:#F3F1F8; font-weight:600; }"
        "#folderRailCount { background:%5; color:#fff; font-size:10px; font-weight:700;"
        "  border-radius:9px; min-width:18px; padding:1px 4px; }"
        "#folderRailEdit { border:none; border-radius:12px; background:transparent; color:#ACA6BD;"
        "  font-size:16px; padding:0; }"
        "#folderRailEdit:hover { background:%2; color:#F3F1F8; }"
        "#scrollDownBtn { background:#221F2C; border:1px solid rgba(255,255,255,0.10);"
        "  border-radius:22px; color:#F3F1F8; font-size:14px; padding:0; }"
        "#scrollDownBtn:hover { background:#2B2737; }"
    ).arg(th.surface1.name(), th.surface2.name(), th.surface3.name(), th.bgBase.name(), th.accent)
     .arg(th.accentR).arg(th.accentG).arg(th.accentB);
}

void ChatPage::applyTheme() {
    setStyleSheet(chatQSS());

    // Инлайн-фоны сообщений/пустой страницы/тем форума.
    const auto th = ThemePreset::current();
    if (msgContainer_) msgContainer_->setStyleSheet(QStringLiteral("#msgContainer{background:%1;}").arg(th.bgBase.name()));
    if (emptyPage_)    emptyPage_->setStyleSheet(QStringLiteral("background:%1;").arg(th.bgBase.name()));
    if (topicsPage_)   topicsPage_->setStyleSheet(QStringLiteral("background:%1;").arg(th.bgBase.name()));
    // Перерисовать бабблы под новую поверхность входящих.
    for (QFrame* b : msgContainer_->findChildren<QFrame*>()) {
        if (b->objectName() == QStringLiteral("bubbleIn"))  b->setStyleSheet(bubbleInQss());
        else if (b->objectName() == QStringLiteral("bubbleOut")) b->setStyleSheet(bubbleOutQss());
    }
}


// ── App-меню: шторка слева 304px, 1:1 с .app-menu веба ────────────────────────
void ChatPage::toggleAppMenu() {
    if (appMenu_ && appMenu_->isVisible()) { closeAppMenu(); return; }
    if (!appMenu_) buildAppMenu();
    appMenuScrim_->setGeometry(0, 0, width(), height());
    appMenuScrim_->show();
    appMenuScrim_->raise();
    appMenu_->setGeometry(-304, 0, 304, height());
    appMenu_->show();
    appMenu_->raise();
    auto* anim = new QPropertyAnimation(appMenu_, "pos", appMenu_);
    anim->setDuration(220);
    anim->setStartValue(QPoint(-304, 0));
    anim->setEndValue(QPoint(0, 0));
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void ChatPage::closeAppMenu() {
    if (!appMenu_ || !appMenu_->isVisible()) return;
    auto* anim = new QPropertyAnimation(appMenu_, "pos", appMenu_);
    anim->setDuration(180);
    anim->setStartValue(QPoint(0, 0));
    anim->setEndValue(QPoint(-304, 0));
    anim->setEasingCurve(QEasingCurve::InCubic);
    connect(anim, &QPropertyAnimation::finished, appMenu_, &QWidget::hide);
    connect(anim, &QPropertyAnimation::finished, appMenuScrim_, &QWidget::hide);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void ChatPage::buildAppMenu() {
    appMenuScrim_ = new QWidget(this);
    appMenuScrim_->setObjectName(QStringLiteral("appMenuScrim"));
    appMenuScrim_->setStyleSheet(QStringLiteral("background:rgba(4,3,8,0.35);"));
    appMenuScrim_->installEventFilter(this);
    appMenuScrim_->hide();

    appMenu_ = new QWidget(this);
    appMenu_->setObjectName(QStringLiteral("appMenu"));
    appMenu_->setStyleSheet(QStringLiteral(R"QSS(
#appMenu { background:#131218; }
#appMenuHeader { border-bottom:1px solid rgba(255,255,255,0.06); }
#appMenuName { color:#F3F1F8; font-size:15px; font-weight:600; }
#appMenuStatus { color:#726C82; font-size:12px; }
#appMenuClose { border:none; background:transparent; color:#ACA6BD; font-size:16px; padding:0; border-radius:8px; }
#appMenuClose:hover { background:rgba(255,255,255,0.05); color:#F3F1F8; }
#appMenuItem { text-align:left; border:none; background:transparent; color:#F3F1F8;
    font-size:15px; font-weight:400; padding:10px 12px; border-radius:10px; }
#appMenuItem:hover { background:rgba(255,255,255,0.05); }
)QSS"));
    auto* root = new QVBoxLayout(appMenu_);
    root->setContentsMargins(10, 10, 10, 14);
    root->setSpacing(2);

    // Шапка: аватар + имя + «в сети» + ✕ (как .app-menu-header веба).
    auto* head = new QWidget(appMenu_);
    head->setObjectName(QStringLiteral("appMenuHeader"));
    auto* hl = new QHBoxLayout(head);
    hl->setContentsMargins(8, 8, 4, 14);
    hl->setSpacing(12);
    auto* av = new QLabel(head);
    Avatar::setRound(av, QString(), Session::instance().username, 44);
    hl->addWidget(av);
    auto* idCol = new QVBoxLayout();
    idCol->setSpacing(1);
    auto* nm = new QLabel(Session::instance().username, head);
    nm->setObjectName(QStringLiteral("appMenuName"));
    auto* stl = new QLabel(QStringLiteral("в сети"), head);
    stl->setObjectName(QStringLiteral("appMenuStatus"));
    idCol->addWidget(nm);
    idCol->addWidget(stl);
    hl->addLayout(idCol, 1);
    auto* close = new QPushButton(QStringLiteral("✕"), head);
    close->setObjectName(QStringLiteral("appMenuClose"));
    close->setFixedSize(32, 32);
    connect(close, &QPushButton::clicked, this, &ChatPage::closeAppMenu);
    hl->addWidget(close, 0, Qt::AlignTop);
    root->addWidget(head);

    // Пункты — как app-menu-item веба (Профиль/группа/канал/Контакты/
    // Сохранённые/Настройки/Каталог). Созданные диалоги — ModalOverlay:
    // при закрытии удаляются через deleteLater (утечек нет).
    auto addItem = [&](const QString& text, std::function<void()> act) {
        auto* btn = new QPushButton(text, appMenu_);
        btn->setObjectName(QStringLiteral("appMenuItem"));
        btn->setCursor(Qt::PointingHandCursor);
        connect(btn, &QPushButton::clicked, this, [this, act]() {
            closeAppMenu();
            act();
        });
        root->addWidget(btn);
    };
    addItem(QStringLiteral("Профиль"), [this]() { openSettings(); });
    addItem(QStringLiteral("Создать группу"), [this]() {
        auto* d = new CreateGroupDialog(api_, window());
        connect(d, &CreateGroupDialog::created, this, [this]() { api_->getGroups(); });
        d->showAnimated();
    });
    addItem(QStringLiteral("Создать канал"), [this]() {
        auto* d = new CreateChannelDialog(api_, window());
        connect(d, &CreateChannelDialog::created, this, [this]() { api_->getChannels(); });
        d->showAnimated();
    });
    addItem(QStringLiteral("Контакты"), [this]() { openNewChatDialog(); });
    addItem(QStringLiteral("Сохранённые сообщения"), [this]() {
        for (const Chat& c : chats_) if (c.isSaved) { openChat(c); return; }
        openChatWith(Session::instance().userId, QStringLiteral("Избранное"),
                     Session::instance().username);
    });
    addItem(QStringLiteral("Настройки"), [this]() { openSettings(); });
    addItem(QStringLiteral("Каталог"), [this]() { openCatalog(); });
    root->addStretch();
    appMenu_->hide();
}

void ChatPage::openSettings() {
    if (settings_) return;   // уже открыто — не плодим копии
    settings_ = new SettingsDialog(api_, window());
    connect(settings_, &ModalOverlay::closed, this, [this]() { settings_ = nullptr; });
    // «Выйти из аккаунта» из ⋮-меню настроек → штатный logout (чистка кэшей).
    connect(settings_, &SettingsDialog::logoutRequested, this, &ChatPage::logoutRequested);
    // Смена темы «Оформления» — мгновенная перегенерация стилей чата.
    connect(settings_, &SettingsDialog::themeChanged, this, &ChatPage::applyTheme);
    settings_->showAnimated();
}

void ChatPage::openNewChatDialog() {
    // Встроенная панель контактов (в стиле Telegram), а не отдельное окно.
    auto* panel = new ContactsPanel(api_, window());
    connect(panel, &ContactsPanel::openChatRequested, this,
            [this](const QString& id, const QString& displayName, const QString& username) {
        openChatWith(id, displayName, username);
    });
    connect(panel, &ContactsPanel::friendsChanged, this, [this]() { api_->getChats(); });
    panel->showAnimated();
}

void ChatPage::openChatWith(const QString& userId, const QString& displayName, const QString& username) {
    const int idx = indexOfChat(userId);
    if (idx >= 0) {
        openChat(chats_[idx]);
        return;
    }
    // Чата ещё нет в списке — открываем «на лету».
    Chat c;
    c.id = userId;
    c.name = username;
    c.displayName = displayName.isEmpty() ? username : displayName;
    openChat(c);
}

void ChatPage::injectForDesignTest(const QList<Chat>& chats, const QList<Folder>& folders,
                                   const QString& openChatId, const QList<ChatMessage>& messages,
                                   const QStringList& pinnedKeys) {
    personalChats_ = chats;
    folders_ = folders;
    pinnedChats_ = QSet<QString>(pinnedKeys.begin(), pinnedKeys.end());
    if (folders.isEmpty()) activeFolderId_ = QStringLiteral("all");
    mergeAllChats();
    const int idx = indexOfChat(openChatId);
    if (idx >= 0) {
        openChat(chats_[idx]);        // сетевые запросы уйдут в offline и тихо зафейлятся
        applyMessages(messages);      // мгновенно рисуем историю без сети
    }
}

// ── Эмодзи ───────────────────────────────────────────────────────────────────

void ChatPage::onEmojiClicked() {
    if (!emojiPicker_) {
        emojiPicker_ = new EmojiPicker(this);
        connect(emojiPicker_, &EmojiPicker::emojiPicked, this, [this](const QString& e) {
            composer_->insertPlainText(e);   // вставляем в позицию курсора, пикер не закрываем
        });
        connect(emojiPicker_, &EmojiPicker::backspacePressed, this, [this]() {
            QTextCursor c = composer_->textCursor();
            if (c.hasSelection()) c.removeSelectedText();
            else c.deletePreviousChar();   // ⌫ в панели, как в Telegram
            composer_->setTextCursor(c);
        });
        // Подарок из панели: только ЛС (не «Избранное», не группы/каналы/боты).
        emojiPicker_->setGiftApi(api_);
        connect(emojiPicker_, &EmojiPicker::giftSendRequested, this,
                [this](const QString& giftId, const QString& name) {
            const bool dm = currentKind_ == ChatKind::User && !currentPeerId_.isEmpty()
                            && currentPeerId_ != Session::instance().userId;
            if (!dm) {
                showInfoOverlay(window(), QStringLiteral("Подарки"),
                                QStringLiteral("Подарки можно отправлять только в личных чатах"));
                return;
            }
            if (QMessageBox::question(this, QStringLiteral("Подарок"),
                    QStringLiteral("Отправить «%1»?").arg(name)) != QMessageBox::Yes) return;
            emojiPicker_->hide();
            api_->giftSend(giftId, currentPeerId_, QString(), false);
            showInfoOverlay(window(), QStringLiteral("Подарок"),
                            QStringLiteral("Отправляем…"));
        });
        connect(api_, &ApiClient::giftSent, this, [this](bool ok, const QString& msg) {
            if (!emojiPicker_) return;
            showInfoOverlay(window(), QStringLiteral("Подарок"),
                ok ? QStringLiteral("Подарок отправлен")
                   : (msg.isEmpty() ? QStringLiteral("Не удалось отправить подарок") : msg));
        });
    }
    // Панель — как .tg-emoji-panel веба: над композером, прижата к правому
    // краю, НЕ попап — остаётся открытой при вводе. Размер клампится под окно.
    const bool dm = currentKind_ == ChatKind::User
                    && currentPeerId_ != Session::instance().userId;
    emojiPicker_->setGiftsAvailable(dm);
    emojiPicker_->toggleAbove(composerBar_);
    if (emojiPicker_->isVisible()) composer_->setFocus();
}

// ── Вложения (скрепка) ───────────────────────────────────────────────────────

void ChatPage::onAttachClicked() {
    const QColor mclr(0xAC, 0xA6, 0xBD);
    QMenu menu(this);
    QAction* photoAct = menu.addAction(Icons::icon(Icons::Image, 18, mclr), QStringLiteral("Фото"));
    QAction* fileAct = menu.addAction(Icons::icon(Icons::File, 18, mclr), QStringLiteral("Файл"));
    QAction* checklist = menu.addAction(Icons::icon(Icons::Checklist, 18, mclr),
                                        QStringLiteral("Чек-лист"));
    menu.addSeparator();
    QMenu* geo = menu.addMenu(QStringLiteral("Геопозиция"));
    geo->setIcon(Icons::icon(Icons::Location, 18, mclr));
    QAction* geoSend = geo->addAction(QStringLiteral("Отправить"));
    QAction* geoLive = geo->addAction(QStringLiteral("Транслировать (Live, скоро)"));
    geoLive->setEnabled(false);

    connect(photoAct, &QAction::triggered, this, &ChatPage::pickAndSendPhoto);
    connect(fileAct, &QAction::triggered, this, &ChatPage::pickAndSendFile);
    connect(checklist, &QAction::triggered, this, &ChatPage::openChecklistDialog);
    connect(geoSend, &QAction::triggered, this, &ChatPage::sendLocation);
    QPoint pos = attachBtn_->mapToGlobal(QPoint(0, 0));
    pos.setY(pos.y() - menu.sizeHint().height() - 6);   // открываем ВВЕРХ
    menu.exec(pos);
}

// ── Таймер исчезающих (пока UI-состояние) ────────────────────────────────────

void ChatPage::onTimerClicked() {
    QMenu menu(this);
    const QList<QPair<QString, int>> opts = {
        {QStringLiteral("Выключено"), 0}, {QStringLiteral("5 секунд"), 5},
        {QStringLiteral("10 секунд"), 10}, {QStringLiteral("30 секунд"), 30},
        {QStringLiteral("1 минута"), 60}, {QStringLiteral("1 час"), 3600},
        {QStringLiteral("24 часа"), 86400}};
    for (const auto& o : opts) {
        QAction* a = menu.addAction(o.first);
        a->setCheckable(true);
        a->setChecked(disappearTtl_ == o.second);
        const int v = o.second;
        connect(a, &QAction::triggered, this, [this, v]() {
            disappearTtl_ = v;
            timerBtn_->setStyleSheet(v > 0
                ? QStringLiteral("#composerIcon{color:#8B5CF6;}") : QString());
        });
    }
    QPoint pos = timerBtn_->mapToGlobal(QPoint(0, 0));
    pos.setY(pos.y() - menu.sizeHint().height() - 6);   // открываем ВВЕРХ
    menu.exec(pos);
}

// ── Файлы ────────────────────────────────────────────────────────────────────

void ChatPage::pickAndSendFile() {
    if (currentPeerId_.isEmpty()) return;
    const QString fn = QFileDialog::getOpenFileName(this, QStringLiteral("Выберите файл"));
    if (fn.isEmpty()) return;
    sendLocalFile(fn, /*asImage*/ false);
}

void ChatPage::pickAndSendPhoto() {
    if (currentPeerId_.isEmpty()) return;
    const QString fn = QFileDialog::getOpenFileName(this, QStringLiteral("Выберите фото"),
        QString(), QStringLiteral("Изображения (*.jpg *.jpeg *.png *.gif *.webp)"));
    if (fn.isEmpty()) return;
    sendLocalFile(fn, /*asImage*/ true);
}

// Один путь отправки локального файла (диалог / дроп / стейджинг): оптимистичный
// баббл с локальным путём + upload; эхо fileUploaded шлёт send-message.
void ChatPage::sendLocalFile(const QString& path, bool asImage) {
    if (currentPeerId_.isEmpty() || path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QByteArray bytes = f.readAll();
    f.close();

    const QString base = QFileInfo(path).fileName();
    const QString tempId = QStringLiteral("tmpf_%1").arg(++tempCounter_);
    shownIds_.insert(tempId);
    pendingFileReceiver_ = currentPeerId_;
    if (asImage) pendingPhotoIds_.insert(tempId);   // пометка: это фото, а не файл

    ChatMessage m;
    m.id = tempId;
    m.sent = true;
    m.status = QStringLiteral("sent");
    m.messageType = asImage ? QStringLiteral("image") : QStringLiteral("file");
    m.fileName = base;
    m.fileSize = bytes.size();
    m.filePath = path;   // локальный путь → клик/превью работают сразу
    m.time = QTime::currentTime().toString(QStringLiteral("HH:mm"));
    m.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    currentMessages_.append(m);
    addBubble(m);
    scrollToBottom();

    api_->uploadFile(bytes, base, tempId);
}

// ── Drag-n-drop аттачей (MLT-06) ─────────────────────────────────────────────

static bool stagedIsImage(const QString& name) {
    static const char* kExt[] = {".jpg", ".jpeg", ".png", ".gif", ".webp", ".bmp"};
    const QString low = name.toLower();
    for (const char* e : kExt)
        if (low.endsWith(QLatin1String(e))) return true;
    return false;
}

void ChatPage::stageFiles(const QList<QUrl>& urls) {
    int added = 0;
    for (const QUrl& u : urls) {
        if (!u.isLocalFile()) continue;
        const QFileInfo fi(u.toLocalFile());
        if (!fi.isFile() || fi.size() <= 0) continue;
        StagedAttachment a;
        a.path = fi.absoluteFilePath();
        a.name = fi.fileName();
        a.size = fi.size();
        a.isImage = stagedIsImage(a.name);
        stagedFiles_.append(a);
        ++added;
    }
    if (added > 0) {
        renderStagedBar();
        composer_->setFocus();   // дальше можно сразу дописать подпись и отправить
    }
}

void ChatPage::renderStagedBar() {
    if (!stagedLay_) return;
    // Пересобираем ряд: чип = превью 56px (или иконка) + имя + размер + ✕.
    while (stagedLay_->count() > 0) {
        QLayoutItem* it = stagedLay_->takeAt(0);
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    stagedBar_->setVisible(!stagedFiles_.isEmpty());
    if (stagedFiles_.isEmpty()) return;

    auto* head = new QLabel(stagedFiles_.size() == 1
        ? QStringLiteral("Вложение к отправке")
        : QStringLiteral("Вложений к отправке: %1").arg(stagedFiles_.size()), stagedBar_);
    head->setStyleSheet(QStringLiteral("color:#726C82;font-size:11px;font-weight:700;"
                                       "text-transform:uppercase;padding:0 8px;"));
    stagedLay_->addWidget(head);

    auto* row = new QWidget(stagedBar_);
    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(8);
    for (int i = 0; i < stagedFiles_.size(); ++i) {
        const StagedAttachment& a = stagedFiles_[i];
        auto* chip = new QFrame(row);
        chip->setObjectName(QStringLiteral("stagedChip"));
        chip->setFixedSize(220, 68);
        auto* cl = new QHBoxLayout(chip);
        cl->setContentsMargins(6, 6, 6, 6);
        cl->setSpacing(8);

        auto* thumb = new QLabel(chip);
        thumb->setFixedSize(56, 56);
        thumb->setAlignment(Qt::AlignCenter);
        thumb->setStyleSheet(QStringLiteral("background:#221F2C;border-radius:8px;"));
        if (a.isImage) {
            QPixmap pm(a.path);
            if (!pm.isNull())
                thumb->setPixmap(pm.scaled(56, 56, Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation));
        } else {
            thumb->setText(QStringLiteral("📎"));
        }
        cl->addWidget(thumb);

        auto* meta = new QVBoxLayout();
        meta->setSpacing(2);
        auto* nm = new QLabel(elide(a.name, QFont(), 120), chip);
        nm->setStyleSheet(QStringLiteral("color:#F3F1F8;font-size:12px;font-weight:600;"));
        const QString mb = a.size < 1024*1024
            ? QStringLiteral("%1 КБ").arg(a.size / 1024)
            : QStringLiteral("%1 МБ").arg(QString::number(a.size / (1024.0 * 1024.0), 'f', 1));
        auto* sz = new QLabel(mb, chip);
        sz->setStyleSheet(QStringLiteral("color:#726C82;font-size:11px;"));
        meta->addWidget(nm);
        meta->addWidget(sz);
        cl->addLayout(meta, 1);

        auto* x = new QPushButton(QStringLiteral("✕"), chip);
        x->setObjectName(QStringLiteral("stagedRemove"));
        x->setCursor(Qt::PointingHandCursor);
        x->setFixedSize(22, 22);
        x->setStyleSheet(QStringLiteral("background:transparent;border:none;color:#726C82;"
                                        "font-size:13px;"));
        const int idx = i;
        connect(x, &QPushButton::clicked, this, [this, idx]() {
            if (idx >= 0 && idx < stagedFiles_.size()) {
                stagedFiles_.removeAt(idx);
                renderStagedBar();
            }
        });
        cl->addWidget(x, 0, Qt::AlignTop);
        rl->addWidget(chip);
    }
    auto* scrollWrap = new QScrollArea(stagedBar_);
    scrollWrap->setWidget(row);
    scrollWrap->setWidgetResizable(true);
    scrollWrap->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollWrap->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollWrap->setFrameShape(QFrame::NoFrame);
    scrollWrap->setFixedHeight(76);
    stagedLay_->addWidget(scrollWrap);
}

void ChatPage::clearStagedFiles() {
    stagedFiles_.clear();
    renderStagedBar();
}

void ChatPage::setDropOverlayActive(bool on) {
    if (!dropOverlay_) return;
    if (on) {
        dropOverlay_->setGeometry(rect());
        dropOverlay_->raise();
        dropOverlay_->show();
    } else {
        dropOverlay_->hide();
    }
}

void ChatPage::dragEnterEvent(QDragEnterEvent* e) {
    if (!e->mimeData()->hasUrls()) { QWidget::dragEnterEvent(e); return; }
    for (const QUrl& u : e->mimeData()->urls())
        if (u.isLocalFile()) { e->acceptProposedAction(); setDropOverlayActive(true); return; }
}

void ChatPage::dragMoveEvent(QDragMoveEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void ChatPage::dragLeaveEvent(QDragLeaveEvent* e) {
    setDropOverlayActive(false);
    QWidget::dragLeaveEvent(e);
}

void ChatPage::dropEvent(QDropEvent* e) {
    if (!e->mimeData()->hasUrls()) { QWidget::dropEvent(e); return; }
    e->acceptProposedAction();
    setDropOverlayActive(false);
    stageFiles(e->mimeData()->urls());
}

void ChatPage::sendPhotoBytesTo(const QString& receiverId, const QByteArray& bytes,
                                const QString& fileName) {
    if (bytes.isEmpty() || receiverId.isEmpty()) return;
    const QString tempId = QStringLiteral("tmpf_%1").arg(++tempCounter_);
    pendingPhotoIds_.insert(tempId);
    pendingFileReceiver_ = receiverId;
    shownIds_.insert(tempId);
    api_->uploadFile(bytes, fileName, tempId);
}

void ChatPage::sendPhotoBytes(const QByteArray& bytes, const QString& fileName) {
    if (currentPeerId_.isEmpty() || bytes.isEmpty()) return;
    // Сохраняем во временный файл, чтобы сразу показать превью.
    const QString tmpPath = QDir::tempPath() + QStringLiteral("/xipher_%1_%2")
        .arg(QString::number(QDateTime::currentMSecsSinceEpoch()), fileName);
    { QFile tf(tmpPath); if (tf.open(QIODevice::WriteOnly)) { tf.write(bytes); tf.close(); } }

    const QString tempId = QStringLiteral("tmpf_%1").arg(++tempCounter_);
    shownIds_.insert(tempId);
    pendingFileReceiver_ = currentPeerId_;
    pendingPhotoIds_.insert(tempId);

    ChatMessage m;
    m.id = tempId; m.sent = true; m.status = QStringLiteral("sent");
    m.messageType = QStringLiteral("image");
    m.fileName = fileName; m.fileSize = bytes.size();
    m.filePath = tmpPath;
    m.time = QTime::currentTime().toString(QStringLiteral("HH:mm"));
    m.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    currentMessages_.append(m);
    addBubble(m);
    scrollToBottom();

    api_->uploadFile(bytes, fileName, tempId);
}

void ChatPage::openChecklistDialog() {
    if (currentPeerId_.isEmpty()) return;

    // Оверлей поверх приложения (как модалки в Telegram), не отдельное окно.
    auto* overlay = new ModalOverlay(window(), 440);
    auto* editor = new ChecklistEditor(overlay->card());
    overlay->cardLayout()->addWidget(editor);

    connect(editor, &ChecklistEditor::cancelled, overlay, &ModalOverlay::closeAnimated);
    connect(editor, &ChecklistEditor::submitted, this,
            [this, overlay](const QJsonObject& payload) {
        overlay->closeAnimated();
        const QString content = ChecklistProto::kPrefix +
            QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
        const QString tempId = QStringLiteral("clk_%1").arg(++tempCounter_);
        shownIds_.insert(tempId);

        ChatMessage m;
        m.id = tempId; m.sent = true; m.status = QStringLiteral("sent");
        m.messageType = QStringLiteral("text");
        m.content = content;
        m.time = QTime::currentTime().toString(QStringLiteral("HH:mm"));
        m.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
        currentMessages_.append(m);
        addBubble(m);
        scrollToBottom();

        api_->sendRaw(currentPeerId_, content, QStringLiteral("text"), tempId);
        bumpChat(currentPeerId_, QStringLiteral("Чек-лист"), m.time, false);
    });

    overlay->showAnimated();
}

void ChatPage::sendLocation() {
    if (currentPeerId_.isEmpty()) return;
    pendingLocReceiver_ = currentPeerId_;
    if (!geoNam_) geoNam_ = new QNetworkAccessManager(this);
    // На десктопе нет GPS — берём приблизительные координаты по IP (ip-api.com).
    QNetworkReply* reply = geoNam_->get(QNetworkRequest(QUrl(
        QStringLiteral("http://ip-api.com/json/?fields=status,lat,lon"))));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        double lat = 0, lon = 0;
        bool ok = false;
        if (reply->error() == QNetworkReply::NoError) {
            const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
            if (o.value(QStringLiteral("status")).toString() == QStringLiteral("success")) {
                lat = o.value(QStringLiteral("lat")).toDouble();
                lon = o.value(QStringLiteral("lon")).toDouble();
                ok = true;
            }
        }
        if (!ok || pendingLocReceiver_.isEmpty()) return;

        const QString content = QStringLiteral("https://yandex.ru/maps/?pt=%1,%2&z=16&l=map")
            .arg(lon, 0, 'f', 6).arg(lat, 0, 'f', 6);
        const QString tempId = QStringLiteral("loc_%1").arg(++tempCounter_);
        shownIds_.insert(tempId);

        ChatMessage m;
        m.id = tempId; m.sent = true; m.status = QStringLiteral("sent");
        m.messageType = QStringLiteral("location");
        m.content = content;
        m.time = QTime::currentTime().toString(QStringLiteral("HH:mm"));
        m.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
        if (pendingLocReceiver_ == currentPeerId_) { currentMessages_.append(m); addBubble(m); scrollToBottom(); }

        api_->sendRaw(pendingLocReceiver_, content, QStringLiteral("location"), tempId);
        bumpChat(pendingLocReceiver_, QStringLiteral("Геопозиция"), m.time, false);
        pendingLocReceiver_.clear();
    });
}

void ChatPage::onFileUploaded(const QString& filePath, const QString& fileName,
                              long long fileSize, const QString& tempId) {
    if (pendingFileReceiver_.isEmpty()) return;
    const bool isPhoto = pendingPhotoIds_.remove(tempId);
    if (isPhoto) {
        api_->sendFile(pendingFileReceiver_, filePath, fileName, fileSize, QString(), tempId,
                       QStringLiteral("image"));
        bumpChat(pendingFileReceiver_, QStringLiteral("Фото"),
                 QTime::currentTime().toString(QStringLiteral("HH:mm")), false);
        return;
    }
    const QString caption = QStringLiteral("📎 ") + fileName;
    api_->sendFile(pendingFileReceiver_, filePath, fileName, fileSize, caption, tempId);
    bumpChat(pendingFileReceiver_, caption,
             QTime::currentTime().toString(QStringLiteral("HH:mm")), false);
}

// ── Поиск / звонок / меню / переименование ───────────────────────────────────

// Центрированный инфо-оверлей (нужен методам ботов и звонков ниже).
static void showInfoOverlay(QWidget* host, const QString& title, const QString& text) {
    auto* ov = new ModalOverlay(host, 360);
    auto* t = new QLabel(title, ov->card());
    t->setStyleSheet(QStringLiteral("color:#F3F1F8;font-size:17px;font-weight:800;"));
    auto* b = new QLabel(text, ov->card());
    b->setWordWrap(true);
    b->setStyleSheet(QStringLiteral("color:#ACA6BD;font-size:14px;"));
    auto* ok = new QPushButton(QStringLiteral("Понятно"), ov->card());
    ok->setCursor(Qt::PointingHandCursor);
    ok->setStyleSheet(QStringLiteral(
        "QPushButton{border:none;border-radius:10px;min-height:42px;color:#fff;font-weight:700;"
        "background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #8B5CF6,stop:1 #6D28D9);}"
        "QPushButton:hover{background:#9B72F8;}"));
    ov->cardLayout()->addWidget(t);
    ov->cardLayout()->addWidget(b);
    ov->cardLayout()->addWidget(ok);
    QObject::connect(ok, &QPushButton::clicked, ov, &ModalOverlay::closeAnimated);
    ov->showAnimated();
}

// ── Боты: inline-кнопки, reply-клавиатура, MiniApps (1:1 с вебом) ────────────

// inline_keyboard: строки кнопок {text, callback_data | url | web_app{url}}.
void ChatPage::addInlineKeyboard(QVBoxLayout* bubbleLayout, const ChatMessage& msg) {
    const QJsonArray rows = msg.replyMarkup.value(QStringLiteral("inline_keyboard")).toArray();
    if (rows.isEmpty()) return;
    auto* box = new QWidget(msgContainer_);
    box->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* bl2 = new QVBoxLayout(box);
    bl2->setContentsMargins(0, 6, 0, 2);
    bl2->setSpacing(4);
    for (const QJsonValue& r : rows) {
        const QJsonArray cols = r.toArray();
        auto* rowW = new QWidget(box);
        auto* hl = new QHBoxLayout(rowW);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(4);
        for (const QJsonValue& c : cols) {
            const QJsonObject btn = c.toObject();
            auto* b = new QPushButton(btn.value(QStringLiteral("text")).toString(), rowW);
            b->setObjectName(QStringLiteral("botKeyboardBtn"));
            b->setCursor(Qt::PointingHandCursor);
            const QString cbData = btn.value(QStringLiteral("callback_data")).toString();
            const QString url = btn.value(QStringLiteral("url")).toString();
            const QString webApp = btn.value(QStringLiteral("web_app")).toObject()
                                       .value(QStringLiteral("url")).toString();
            const QString mid = msg.id.startsWith(QStringLiteral("tmp_")) ? QString() : msg.id;
            connect(b, &QPushButton::clicked, this, [this, mid, cbData, url, webApp, msg]() {
                if (!webApp.isEmpty())                       { openMiniapp(webApp, msg.senderId); return; }
                if (!url.isEmpty())                          { QDesktopServices::openUrl(QUrl(url)); return; }
                if (!cbData.isEmpty() && !mid.isEmpty())     api_->botCallback(mid, cbData);
            });
            hl->addWidget(b, 1);
        }
        bl2->addWidget(rowW);
    }
    bubbleLayout->addWidget(box);
}

// reply-клавиатура бота: кнопки над композером (как в Telegram).
void ChatPage::applyReplyKeyboard(const QJsonObject& markup) {
    if (!botKeyboardBar_ || !botKeyboardLayout_) return;
    currentReplyKeyboard_ = markup;
    // Очистить старые ряды.
    QLayoutItem* it;
    while ((it = botKeyboardLayout_->takeAt(0)) != nullptr) {
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    const bool oneTime = markup.value(QStringLiteral("one_time_keyboard")).toBool(false);
    const QJsonArray rows = markup.value(QStringLiteral("keyboard")).toArray();
    for (const QJsonValue& r : rows) {
        const QJsonArray cols = r.toArray();
        auto* rowW = new QWidget(botKeyboardBar_);
        auto* hl = new QHBoxLayout(rowW);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(4);
        for (const QJsonValue& c : cols) {
            const QJsonObject btn = c.toObject();
            const QString text = btn.value(QStringLiteral("text")).toString();
            auto* b = new QPushButton(text, rowW);
            b->setObjectName(QStringLiteral("botKeyboardBtn"));
            b->setCursor(Qt::PointingHandCursor);
            connect(b, &QPushButton::clicked, this, [this, btn, text, oneTime]() {
                if (btn.value(QStringLiteral("request_location")).toBool(false)) {
                    sendLocation();
                } else if (!text.isEmpty()) {
                    composer_->setPlainText(text);
                    onSendClicked();
                }
                if (oneTime) hideBotKeyboard();
            });
            hl->addWidget(b, 1);
        }
        botKeyboardLayout_->addWidget(rowW);
    }
    botKeyboardBar_->setVisible(!rows.isEmpty());
}

void ChatPage::hideBotKeyboard() {
    if (botKeyboardBar_) botKeyboardBar_->setVisible(false);
    currentReplyKeyboard_ = QJsonObject();
}

// MiniApp: подписанный initData от сервера + открытие приложения (как в вебе,
// только в системном браузере — десктоп без webview по дизайну).
void ChatPage::openMiniapp(const QString& url, const QString& botId) {
    if (url.isEmpty()) return;
    pendingMiniappUrl_ = url;
    api_->botMiniappInit(botId);
}

void ChatPage::onMiniappInitReady(bool ok, const QString& initData) {
    QString url = pendingMiniappUrl_;
    pendingMiniappUrl_.clear();
    if (url.isEmpty()) return;
    if (ok && !initData.isEmpty()) {
        const QString sep = url.contains(QLatin1Char('?')) ? QStringLiteral("&") : QStringLiteral("?");
        url += sep + QStringLiteral("init_data=") + QString::fromUtf8(
            QUrl::toPercentEncoding(initData));
    }
    QDesktopServices::openUrl(QUrl(url));
}

void ChatPage::onBotCallbackDone(bool ok, const QJsonObject& response) {
    if (!ok) return;
    // Бот может ответить алертом и/или попросить обновить сообщение.
    const QString alert = response.value(QStringLiteral("alert")).toString();
    if (!alert.isEmpty())
        showInfoOverlay(window(), QStringLiteral("Бот"), alert);
    if (response.value(QStringLiteral("update_message")).toBool(false))
        reloadCurrentMessages();
}

// ── Каталог публичных каналов и групп ────────────────────────────────────────
void ChatPage::openCatalog() {
    auto* d = new CatalogDialog(api_, window());
    connect(d, &CatalogDialog::joined, this, [this]() {
        api_->getChats(); api_->getGroups(); api_->getChannels();
    });
    d->showAnimated();
}

void ChatPage::startCall() {
    if (currentPeerId_.isEmpty()) return;
    if (currentPeerId_ == Session::instance().userId) {   // «Избранные» — себе не звоним
        showInfoOverlay(window(), QStringLiteral("Звонок"),
                        QStringLiteral("Нельзя позвонить в «Избранные»."));
        return;
    }
    const int idx = indexOfChat(currentPeerId_);
    const QString avatar = idx >= 0 ? chats_[idx].avatarUrl : QString();
    emit callRequested(currentPeerId_, currentPeerName_, avatar);
}

void ChatPage::showChatMenu() {
    if (currentPeerId_.isEmpty()) return;
    const QColor mclr(0xAC, 0xA6, 0xBD);
    QMenu menu(this);
    QAction* rename = menu.addAction(Icons::icon(Icons::Pencil, 18, mclr),
                                     QStringLiteral("Изменить имя контакта"));
    QAction* secret = menu.addAction(Icons::icon(Icons::Lock, 18, mclr),
                                     QStringLiteral("Секретный чат (скоро)"));
    connect(rename, &QAction::triggered, this, &ChatPage::openRenameDialog);
    connect(secret, &QAction::triggered, this, [this]() {
        showInfoOverlay(window(), QStringLiteral("Секретный чат"),
            QStringLiteral("Секретные чаты со сквозным шифрованием (Signal-протокол) — "
                           "в работе. На сервере поддержка есть; нужен клиентский E2EE "
                           "(обмен ключами, шифрование сообщений)."));
    });
    menu.exec(moreBtn_->mapToGlobal(QPoint(0, moreBtn_->height() + 4)));
}

void ChatPage::openRenameDialog() {
    if (currentPeerId_.isEmpty()) return;
    auto* ov = new ModalOverlay(window(), 380);
    auto* t = new QLabel(QStringLiteral("Имя контакта"), ov->card());
    t->setStyleSheet(QStringLiteral("color:#F3F1F8;font-size:17px;font-weight:800;"));
    auto* edit = new QLineEdit(currentPeerName_, ov->card());
    edit->setStyleSheet(QStringLiteral(
        "QLineEdit{background:#1A1822;border:1px solid rgba(255,255,255,0.10);border-radius:10px;"
        "min-height:40px;padding:0 12px;color:#F3F1F8;font-size:14px;}"
        "QLineEdit:focus{border:1px solid #8B5CF6;}"));
    auto* row = new QHBoxLayout();
    row->addStretch();
    auto* cancel = new QPushButton(QStringLiteral("Отмена"), ov->card());
    cancel->setCursor(Qt::PointingHandCursor);
    cancel->setStyleSheet(QStringLiteral(
        "QPushButton{border:1px solid rgba(255,255,255,0.14);border-radius:10px;padding:9px 14px;color:#ACA6BD;background:transparent;}"
        "QPushButton:hover{color:#F3F1F8;border-color:#8B5CF6;}"));
    auto* save = new QPushButton(QStringLiteral("Сохранить"), ov->card());
    save->setCursor(Qt::PointingHandCursor);
    save->setStyleSheet(QStringLiteral(
        "QPushButton{border:none;border-radius:10px;padding:9px 18px;color:#fff;font-weight:700;"
        "background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #8B5CF6,stop:1 #6D28D9);}"
        "QPushButton:hover{background:#9B72F8;}"));
    row->addWidget(cancel);
    row->addWidget(save);
    ov->cardLayout()->addWidget(t);
    ov->cardLayout()->addWidget(edit);
    ov->cardLayout()->addLayout(row);
    connect(cancel, &QPushButton::clicked, ov, &ModalOverlay::closeAnimated);
    const QString peer = currentPeerId_;
    connect(save, &QPushButton::clicked, this, [this, ov, edit, peer]() {
        const QString name = edit->text().trimmed();
        if (!name.isEmpty()) api_->setContactName(peer, name);
        ov->closeAnimated();
    });
    ov->showAnimated();
    edit->setFocus();
    edit->selectAll();
}

// ── Голосовые сообщения ──────────────────────────────────────────────────────

void ChatPage::onMicClicked() {
    if (currentPeerId_.isEmpty() || recorder_->isRecording()) return;
    if (recorder_->start()) {
        composerStack_->setCurrentIndex(1);
        recBar_->start();
    }
}

void ChatPage::cancelRecording() {
    recBar_->stop();
    recorder_->cancel();
    composerStack_->setCurrentIndex(0);
}

void ChatPage::stopAndSendVoice() {
    pendingVoiceSecs_ = recBar_->seconds();   // длительность записи
    recBar_->stop();
    composerStack_->setCurrentIndex(0);
    pendingVoiceReceiver_ = currentPeerId_;
    recorder_->stop();   // → onVoiceRecorded
}

void ChatPage::onVoicePlayPause(VoiceMessageWidget* w, const QString& path) {
    // Тот же виджет — пауза/продолжение.
    if (activeVoice_ == w) {
        if (player_->playbackState() == QMediaPlayer::PlayingState) {
            player_->pause();
            w->setPlaying(false);
        } else {
            player_->play();
            w->setPlaying(true);
        }
        return;
    }
    // Переключение на другой голосовой — сбрасываем предыдущий.
    if (activeVoice_) {
        activeVoice_->setPlaying(false);
        activeVoice_->setProgress(0.0);
    }
    activeVoice_ = w;
    activeVoicePath_ = path;
    w->setPlaying(true);
    playVoice(path);
}

void ChatPage::onVoiceRecorded(const QString& filePath, const QString& mimeType) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QByteArray bytes = f.readAll();
    f.close();
    if (bytes.isEmpty()) return;

    const QString tempId = QStringLiteral("tmpv_%1").arg(++tempCounter_);
    pendingVoiceTempId_ = tempId;
    shownIds_.insert(tempId);

    // Оптимистичный баббл (играем сразу из локального файла).
    ChatMessage m;
    m.id = tempId;
    m.sent = true;
    m.status = QStringLiteral("sent");
    m.messageType = QStringLiteral("voice");
    m.filePath = filePath;   // локальный путь → playVoice сыграет напрямую
    m.content = voiceLabel(pendingVoiceSecs_);   // длительность в подписи
    m.time = QTime::currentTime().toString(QStringLiteral("HH:mm"));
    if (pendingVoiceReceiver_ == currentPeerId_) {
        m.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
        currentMessages_.append(m);
        addBubble(m);
        scrollToBottom();
    }

    api_->uploadVoice(bytes, mimeType, tempId);
}

void ChatPage::onVoiceUploaded(const QString& filePath, const QString& fileName,
                               long long fileSize, const QString& tempId) {
    Q_UNUSED(tempId);
    if (pendingVoiceReceiver_.isEmpty()) return;
    const QString caption = voiceLabel(pendingVoiceSecs_);   // "🎤 m:ss"
    api_->sendVoice(pendingVoiceReceiver_, filePath, fileName, fileSize, caption, pendingVoiceTempId_);
    bumpChat(pendingVoiceReceiver_, caption,
             QTime::currentTime().toString(QStringLiteral("HH:mm")), false);
}

void ChatPage::playVoice(const QString& path) {
    if (path.isEmpty()) return;
    // Локальный путь (оптимистичный или уже скачанный) — играем сразу.
    if (!path.startsWith(QStringLiteral("/files"))) {
        player_->setSource(QUrl::fromLocalFile(path));
        player_->play();
        return;
    }
    if (voiceCache_.contains(path)) {
        player_->setSource(QUrl::fromLocalFile(voiceCache_.value(path)));
        player_->play();
        return;
    }
    // Уже скачивали раньше (в т.ч. в прошлых запусках) — мгновенно из кэша.
    QByteArray vc;
    if (FileCache::instance().lookup(path, &vc)) {
        playVoiceBytes(path, vc);
        return;
    }
    pendingPlayPath_ = path;
    api_->fetchFile(path);   // скачаем с токеном, потом сыграем
}

void ChatPage::playVoiceBytes(const QString& serverPath, const QByteArray& bytes) {
    // Контейнер может быть webm/opus (браузерный MediaRecorder) — расширение
    // берём из серверного пути, иначе QMediaPlayer не откроет демуксер.
    QString suffix = QFileInfo(serverPath).suffix();
    if (suffix.isEmpty()) suffix = QStringLiteral("webm");
    const QString local = QDir::temp().filePath(
        QStringLiteral("xipher_dl_%1.%2").arg(qHash(serverPath)).arg(suffix));
    QFile f(local);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(bytes);
    f.close();
    voiceCache_.insert(serverPath, local);
    if (serverPath == pendingPlayPath_) {
        pendingPlayPath_.clear();
        player_->setSource(QUrl::fromLocalFile(local));
        player_->play();
    }
}

void ChatPage::setBubbleImage(QLabel* img, const QByteArray& bytes) {
    QPixmap pm;
    if (!img || !pm.loadFromData(bytes)) return;
    img->setProperty("imgFull", pm);
    // 1:1 с .message-image img веба: max-width 100%, max-height 400px,
    // радиус 8px — картинка ВПИСЫВАЕТСЯ в границы, а не обрезается/раздувается.
    const int maxW = qBound(240, msgContainer_ ? msgContainer_->width() * 72 / 100 : 452, 480);
    QPixmap scaled = pm.scaled(maxW, 400, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPixmap rounded(scaled.size());
    rounded.fill(Qt::transparent);
    QPainter pt(&rounded);
    pt.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(0, 0, scaled.width(), scaled.height(), 8, 8);
    pt.setClipPath(clip);
    pt.drawPixmap(0, 0, scaled);
    pt.end();
    img->setPixmap(rounded);
    img->setText(QString());
    img->setMinimumSize(0, 0);
    img->setFixedSize(scaled.size());
}

void ChatPage::onFileFetched(const QString& filePath, const QByteArray& bytes) {
    // Картинка для сообщения-фото.
    if (pendingImage_.contains(filePath)) {
        QPointer<QLabel> lbl = pendingImage_.take(filePath);
        setBubbleImage(lbl, bytes);
        FileCache::instance().store(filePath, bytes);   // повторно не качаем
        return;
    }

    // Скачивание обычного файла → сохраняем в «Загрузки» и открываем.
    if (pendingFileOpen_.contains(filePath)) {
        const QString name = pendingFileOpen_.take(filePath);
        QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (dir.isEmpty()) dir = QDir::tempPath();
        const QString dest = QDir(dir).filePath(name);
        QFile out(dest);
        if (out.open(QIODevice::WriteOnly)) {
            out.write(bytes);
            out.close();
            QDesktopServices::openUrl(QUrl::fromLocalFile(dest));
        }
        return;
    }

    // Иначе — голосовое: в зашифрованный кэш (мгновенный повтор, и после
    // перезапуска) и проигрываем.
    FileCache::instance().store(filePath, bytes);
    playVoiceBytes(filePath, bytes);
}
