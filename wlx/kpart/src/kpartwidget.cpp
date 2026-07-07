#include "kpartwidget.h"
#include <QMimeDatabase>
#include <KParts/ReadOnlyPart>
#include <KParts/PartLoader>
#include <KPluginMetaData>
#include <QUrl>
#include <QEvent>
#include <QFocusEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCoreApplication>
#include <QApplication>
#include <QTimer>
#include <QChildEvent>
#include <QResizeEvent>
#include <QEnterEvent>
#include <QScrollBar>
#include <QAbstractScrollArea>

KPartWidget::KPartWidget(QWidget *parent)
    : QWidget(parent)
    , m_part(nullptr)
    , m_loadGeneration(0)
{
    // NoFocus by default — activation is managed exclusively via
    // lc_focus from DC and geometry-based click detection.
    setFocusPolicy(Qt::NoFocus);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    // Install global event filter to intercept focus-stealing by KParts
    // at the application level, regardless of which widget they target.
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->installEventFilter(this);
    }

    // Connect to focusChanged to bounce focus back when inactive.
    // This is the primary defence against KParts stealing focus
    // programmatically (e.g. Okular after rendering a page).
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *old, QWidget *now) {
        if (!m_part || !m_part->widget()) return;

        bool nowInside = now && (now == this || this->isAncestorOf(now));

        if (m_isActive) {
            // If focus left the plugin while active, deactivate.
            bool oldInside = old && (old == this || this->isAncestorOf(old));
            if (oldInside && !nowInside) {
                setActive(false);
            }
        } else {
            // Inactive: if focus entered the plugin, bounce it back.
            if (nowInside) {
                // Learn which widget the KPart naturally wants to focus.
                if (now != this) {
                    m_partFocusWidget = now;
                }
                QPointer<QWidget> pOld(old);
                QTimer::singleShot(0, this, [this, pOld]() {
                    QWidget *currentFocus = QApplication::focusWidget();
                    bool stillInside = currentFocus &&
                        (currentFocus == this || this->isAncestorOf(currentFocus));
                    if (stillInside) {
                        if (pOld) {
                            pOld->setFocus(Qt::OtherFocusReason);
                        } else {
                            restoreFocusToDC();
                        }
                    }
                });
            }
        }
    });
}

KPartWidget::~KPartWidget()
{
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->removeEventFilter(this);
    }
    if (m_part) {
        m_part->closeUrl();
        delete m_part;
    }
}

void KPartWidget::returnFocusToDC()
{
    setActive(false);
}

void KPartWidget::restoreFocusToDC()
{
    if (m_savedFocusWidget) {
        m_savedFocusWidget->setFocus(Qt::OtherFocusReason);
    } else if (this->parentWidget()) {
        this->parentWidget()->setFocus(Qt::OtherFocusReason);
    }
}

void KPartWidget::setActive(bool active)
{
    if (m_isActive == active)
        return;

    m_isActive = active;

    if (!active) {
        this->clearFocus();
        if (m_part && m_part->widget()) {
            m_part->widget()->clearFocus();
        }
        if (this->parentWidget()) {
            this->parentWidget()->setFocus(Qt::OtherFocusReason);
        }
    } else {
        // Find the best widget to give focus to:
        // 1. m_partFocusWidget (learned from the KPart's own focus steal)
        // 2. Walk the focus proxy chain from m_part->widget()
        // 3. Search for a child with StrongFocus policy
        // 4. Fall back to m_part->widget() itself
        QWidget *target = nullptr;
        if (m_part && m_part->widget()) {
            if (m_partFocusWidget) {
                target = m_partFocusWidget.data();
            } else {
                // Walk focus proxy chain
                target = m_part->widget();
                while (target->focusProxy()) {
                    target = target->focusProxy();
                }
                // If proxy chain led nowhere useful, search children
                if (target == m_part->widget() &&
                    !(target->focusPolicy() & Qt::StrongFocus)) {
                    for (QWidget *child : m_part->widget()->findChildren<QWidget*>()) {
                        if (child->focusPolicy() & Qt::StrongFocus) {
                            target = child;
                            break;
                        }
                    }
                }
            }
            target->setFocus(Qt::OtherFocusReason);
        }
    }
}

void KPartWidget::installFocusGuard()
{
    if (!m_part || !m_part->widget()) return;

    // Install event filters to intercept mouse/key/focus events, but do NOT
    // override focus policies.  The focusChanged bounce-back (see constructor)
    // is the sole defence against programmatic focus steals.  Keeping the
    // native focus policies lets KPart widgets actually receive keyboard
    // input (arrows, pgup/down, home/end) when the plugin is active.
    m_part->widget()->installEventFilter(this);
    m_part->widget()->setAttribute(Qt::WA_NativeWindow, false);

    for (QWidget *child : m_part->widget()->findChildren<QWidget*>()) {
        child->setAttribute(Qt::WA_NativeWindow, false);
        child->installEventFilter(this);
    }
}

bool KPartWidget::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        // Geometry-based click detection, mirroring FocusManager pattern.
        if (!m_part || !m_part->widget()) break;

        QMouseEvent *me = static_cast<QMouseEvent*>(event);
        const QPoint gp = me->globalPosition().toPoint();
        const QRect gr(mapToGlobal(QPoint(0, 0)), size());

        if (m_isActive && !gr.contains(gp)) {
            setActive(false);
        } else if (!m_isActive && gr.contains(gp)) {
            // Following FocusManager pattern: just set the flag.
            // Let the click event propagate naturally so Qt gives
            // click-focus to the widget the user actually clicked.
            m_isActive = true;
        }
        break;
    }
    case QEvent::KeyPress: {
        if (m_isActive) {
            QKeyEvent *ke = static_cast<QKeyEvent*>(event);
            // Ctrl+Q: deactivate and forward to DC.
            if (ke->key() == Qt::Key_Q && (ke->modifiers() & Qt::ControlModifier)) {
                setActive(false);
                QTimer::singleShot(0, this, [this]() {
                    QWidget *target = QApplication::activeWindow();
                    if (!target) target = this->window();
                    if (target) {
                        QCoreApplication::postEvent(target,
                            new QKeyEvent(QEvent::KeyPress, Qt::Key_Q, Qt::ControlModifier));
                        QCoreApplication::postEvent(target,
                            new QKeyEvent(QEvent::KeyRelease, Qt::Key_Q, Qt::ControlModifier));
                    }
                });
                return true;
            }
            // Navigation keys: if the event has propagated up to KPartWidget,
            // it means the KPart didn't handle it (e.g. KTextEditor in
            // read-only embedded mode). Scroll the view ourselves.
            if (watched == this) {
                switch (ke->key()) {
                case Qt::Key_Up:
                case Qt::Key_Down:
                case Qt::Key_PageUp:
                case Qt::Key_PageDown:
                case Qt::Key_Home:
                case Qt::Key_End:
                    if (scrollView(ke->key())) {
                        return true;
                    }
                    break;
                }
            }
        }
        break;
    }
    case QEvent::ChildAdded: {
        // Okular/Calligra spawn widgets asynchronously (e.g. PageView).
        // Apply focus guards to each new child in our KPart's subtree.
        QChildEvent *ce = static_cast<QChildEvent*>(event);
        if (ce->child() && ce->child()->isWidgetType()) {
            QWidget *childWidget = static_cast<QWidget*>(ce->child());
            if (m_part && m_part->widget() &&
                (watched == m_part->widget() || m_part->widget()->isAncestorOf(static_cast<QWidget*>(watched)))) {
                childWidget->setAttribute(Qt::WA_NativeWindow, false);
                childWidget->installEventFilter(this);
            }
        }
        break;
    }

    case QEvent::FocusIn: {
        // When inactive, block any programmatic focus entry into the
        // plugin subtree. The focusChanged connection above handles the
        // bounce, but this provides belt-and-suspenders protection.
        if (!m_isActive && watched->isWidgetType()) {
            QWidget *w = static_cast<QWidget*>(watched);
            bool isOurs = (w == this);
            if (!isOurs && m_part && m_part->widget()) {
                isOurs = (w == m_part->widget() || m_part->widget()->isAncestorOf(w));
            }
            if (isOurs) {
                QFocusEvent *fe = static_cast<QFocusEvent*>(event);
                if (fe->reason() == Qt::OtherFocusReason ||
                    fe->reason() == Qt::ActiveWindowFocusReason) {
                    // Programmatic focus steal while inactive — bounce it
                    QTimer::singleShot(0, this, [this]() {
                        restoreFocusToDC();
                    });
                }
            }
        }
        break;
    }

    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

bool KPartWidget::loadFile(const QString &fileName)
{
    // Save which widget currently has focus (DC's file list) so we can
    // restore it after the KPart inevitably steals focus.
    m_savedFocusWidget = QApplication::focusWidget();

    // Increment generation to invalidate any queued callbacks from the
    // previous part before we tear it down.
    m_loadGeneration++;

    if (m_part) {
        // Deactivate but don't try to restore focus yet — we're about to
        // tear down and rebuild the part.
        m_isActive = false;
        m_partFocusWidget = nullptr;

        m_part->closeUrl();
        m_layout->removeWidget(m_part->widget());
        delete m_part;
        m_part = nullptr;
    }

    QMimeDatabase db;
    QMimeType mime = db.mimeTypeForFile(fileName);

    // If the file is detected as a generic ZIP but has a more specific extension
    // (like .docx, .odt, etc.), prioritize the extension-based MIME type.
    if (mime.name() == QLatin1String("application/zip") || mime.isDefault()) {
        QMimeType extMime = db.mimeTypeForFile(fileName, QMimeDatabase::MatchExtension);
        if (!extMime.isDefault() && extMime.name() != mime.name()) {
            mime = extMime;
        }
    }

    QUrl url = QUrl::fromLocalFile(fileName);

    // Find all parts available for this MIME type
    QVector<KPluginMetaData> parts = KParts::PartLoader::partsForMimeType(mime.name());

    KPluginMetaData selectedPart;

    // First pass: look for specialized renderers (not archives, not terminal)
    for (const auto &metaData : parts) {
        QString pluginId = metaData.pluginId();
        if (pluginId.contains(QLatin1String("konsole"), Qt::CaseInsensitive) ||
            pluginId.contains(QLatin1String("arkpart"), Qt::CaseInsensitive) ||
            pluginId.contains(QLatin1String("kioarchive"), Qt::CaseInsensitive)) {
            continue;
        }
        selectedPart = metaData;
        break;
    }

    // Second pass: if no specialized renderer found, allow archive explorers as fallback
    if (!selectedPart.isValid()) {
        for (const auto &metaData : parts) {
            QString pluginId = metaData.pluginId();
            if (pluginId.contains(QLatin1String("konsole"), Qt::CaseInsensitive)) {
                continue;
            }
            if (pluginId.contains(QLatin1String("arkpart"), Qt::CaseInsensitive) ||
                pluginId.contains(QLatin1String("kioarchive"), Qt::CaseInsensitive)) {
                selectedPart = metaData;
                break;
            }
        }
    }

    if (selectedPart.isValid()) {
        m_pendingUrl = url;
        m_selectedPart = selectedPart;

        // Defer instantiation by 50ms so Double Commander can finish handling
        // the user's MouseRelease event on the file list. Without this delay,
        // complex KParts spin up Wayland grabs so fast that DC misses the
        // release and gets stuck in a phantom-drag mode.
        QTimer::singleShot(50, this, [this, gen = m_loadGeneration]() {
            if (gen == m_loadGeneration) {
                instantiatePart();
            }
        });

        return true;
    }

    return false;
}

void KPartWidget::instantiatePart()
{
    auto result = KParts::PartLoader::instantiatePart<KParts::ReadOnlyPart>(m_selectedPart, this, this);
    if (result) {
        m_part = result.plugin;

        m_layout->addWidget(m_part->widget());

        installFocusGuard();
        connect(m_part, &KParts::ReadOnlyPart::completed, this, [this]() {
            installFocusGuard();
            restoreFocusToDC();
            if (m_part && m_part->widget()) {
                QTimer::singleShot(300, m_part->widget(), [w = m_part->widget()]() {
                    QCoreApplication::postEvent(w, new QEvent(QEvent::WindowActivate));
                    QCoreApplication::postEvent(w, new QResizeEvent(w->size(), w->size()));
                    QCoreApplication::postEvent(w, new QEnterEvent(QPointF(0,0), QPointF(0,0), QPointF(0,0)));
                    QCoreApplication::postEvent(w, new QEvent(QEvent::Leave));
                    w->update();
                    
                    for (QWidget *child : w->findChildren<QWidget*>()) {
                        QCoreApplication::postEvent(child, new QEvent(QEvent::WindowActivate));
                        QCoreApplication::postEvent(child, new QResizeEvent(child->size(), child->size()));
                        child->update();
                    }
                });
            }
        });
        connect(m_part, &KParts::ReadOnlyPart::completedWithPendingAction, this, [this]() {
            installFocusGuard();
            restoreFocusToDC();
            if (m_part && m_part->widget()) {
                QTimer::singleShot(300, m_part->widget(), [w = m_part->widget()]() {
                    QCoreApplication::postEvent(w, new QEvent(QEvent::WindowActivate));
                    QCoreApplication::postEvent(w, new QResizeEvent(w->size(), w->size()));
                    QCoreApplication::postEvent(w, new QEnterEvent(QPointF(0,0), QPointF(0,0), QPointF(0,0)));
                    QCoreApplication::postEvent(w, new QEvent(QEvent::Leave));
                    w->update();
                    
                    for (QWidget *child : w->findChildren<QWidget*>()) {
                        QCoreApplication::postEvent(child, new QEvent(QEvent::WindowActivate));
                        QCoreApplication::postEvent(child, new QResizeEvent(child->size(), child->size()));
                        child->update();
                    }
                });
            }
        });

        m_part->openUrl(m_pendingUrl);

        // Immediately restore focus after opening (catches synchronous focus steals)
        restoreFocusToDC();
    }
}

bool KPartWidget::scrollView(int key)
{
    if (!m_part || !m_part->widget()) return false;

    // Find the vertical scrollbar inside the KPart's widget tree.
    // Try QAbstractScrollArea first (Okular, Gwenview, etc.), then
    // fall back to any vertical QScrollBar (KTextEditor uses KateScrollBar).
    QScrollBar *vbar = nullptr;

    QAbstractScrollArea *scrollArea = m_part->widget()->findChild<QAbstractScrollArea*>();
    if (scrollArea) {
        vbar = scrollArea->verticalScrollBar();
    }

    if (!vbar || !vbar->maximum()) {
        for (QScrollBar *bar : m_part->widget()->findChildren<QScrollBar*>()) {
            if (bar->orientation() == Qt::Vertical && bar->maximum() > 0) {
                vbar = bar;
                break;
            }
        }
    }

    if (!vbar || vbar->maximum() <= 0) return false;

    switch (key) {
    case Qt::Key_Up:
        vbar->triggerAction(QAbstractSlider::SliderSingleStepSub);
        return true;
    case Qt::Key_Down:
        vbar->triggerAction(QAbstractSlider::SliderSingleStepAdd);
        return true;
    case Qt::Key_PageUp:
        vbar->triggerAction(QAbstractSlider::SliderPageStepSub);
        return true;
    case Qt::Key_PageDown:
        vbar->triggerAction(QAbstractSlider::SliderPageStepAdd);
        return true;
    case Qt::Key_Home:
        vbar->triggerAction(QAbstractSlider::SliderToMinimum);
        return true;
    case Qt::Key_End:
        vbar->triggerAction(QAbstractSlider::SliderToMaximum);
        return true;
    }
    return false;
}
