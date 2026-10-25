#pragma once
#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <functional>
class QLineEdit;
class QLabel;
class QPushButton;
class BlueprintV2EditDialog final : public QDialog {
    Q_OBJECT
public:
    BlueprintV2EditDialog(const QJsonObject &targets, int sourceIndex, const QJsonObject &baselineTargets, const QJsonArray &pending, QWidget *parent=nullptr);
    QJsonArray edits() const;
    void setPending(bool pending);
    void showError(const QString &error);
signals:
    void previewRequested();
private:
    struct Field {QLineEdit *input;QJsonObject target;QString original;bool literal;};
    QList<Field> m_fields;
    QJsonArray m_pending;
    QLabel *m_error;
    QPushButton *m_apply;
protected:
    void reject() override;
private:
    bool m_busy=false;
};
