#pragma once
#include <QWidget>
#include <QJsonObject>
class QGraphicsScene;
class QGraphicsView;
class QPlainTextEdit;
class QLabel;
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
};
