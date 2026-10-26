#pragma once
#include <QWidget>
#include <QJsonObject>
class QGraphicsScene;
class QGraphicsView;
class QPlainTextEdit;
class QLabel;
class QPushButton;
class QShowEvent;
class QResizeEvent;
class BlueprintV2View final : public QWidget {
    Q_OBJECT
public:
    explicit BlueprintV2View(QWidget *parent = nullptr);
    ~BlueprintV2View() override;
    void setReply(const QJsonObject &reply);
    void refreshPresentation();
    QGraphicsView *graphicsView() const { return m_view; }
    int selectedSourceIndex() const;
    void setEditState(bool writable, bool busy, const QString &reason = {});
    void setOperationStatus(const QString &text);
signals:
    void editRequested();
protected:
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
private:
    void showSelection();
    QJsonObject m_reply;
    QGraphicsScene *m_scene;
    QGraphicsView *m_view;
    QPlainTextEdit *m_properties, *m_diagnostics;
    QLabel *m_banner;
    QLabel *m_status;
    QPushButton *m_editButton;
    bool m_writable = false, m_busy = false;
    QString m_readOnlyReason;
};
