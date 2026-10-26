#include "app/blueprint_v2_edit_dialog.h"
#include <QFormLayout>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QJsonDocument>
namespace {
QString key(QJsonObject edit) {edit.remove("value");edit.remove("valueLiteral");return QString::fromUtf8(QJsonDocument(edit).toJson(QJsonDocument::Compact));}
bool sameLiteral(const QString &left,const QString &right) {
    QJsonParseError a,b;auto x=QJsonDocument::fromJson(("["+left+"]").toUtf8(),&a);auto y=QJsonDocument::fromJson(("["+right+"]").toUtf8(),&b);
    if(a.error!=QJsonParseError::NoError||b.error!=QJsonParseError::NoError||x.array().size()!=1||y.array().size()!=1)return false;
    if(x.array()[0].isDouble()&&y.array()[0].isDouble()&&x.array()[0].toDouble()==0&&y.array()[0].toDouble()==0)return left.trimmed().startsWith('-')==right.trimmed().startsWith('-');
    return x.array()[0]==y.array()[0];
}
}
BlueprintV2EditDialog::BlueprintV2EditDialog(const QJsonObject &targets,int sourceIndex,const QJsonObject &baseline,const QJsonArray &pending,QWidget *parent):QDialog(parent),m_pending(pending) {
    setObjectName("v2EditValuesDialog");setWindowTitle(tr("Edit v2 values"));auto *form=new QFormLayout(this);
    auto *note=new QLabel(tr("Only declared fields are editable. Property values are JSON scalar literals, for example: \"text\", true, 12, null. Preview does not save."),this);note->setWordWrap(true);form->addRow(note);
    auto add=[&](const QString &label,const QString &name,const QString &value,const QString &original,QJsonObject target,bool literal){auto *edit=new QLineEdit(value,this);edit->setObjectName(name);form->addRow(label,edit);m_fields.append({edit,target,original,literal});};
    add(tr("Project name"),"v2ProjectNameEdit",targets["projectName"].toString(),baseline["projectName"].toString(),{{"kind","project-name"}},false);
    QJsonObject baseNode;for(auto n:baseline["nodes"].toArray())if(n.toObject()["sourceIndex"]==sourceIndex)baseNode=n.toObject();
    for(auto value:targets["nodes"].toArray()) {auto node=value.toObject();if(node["sourceIndex"]!=sourceIndex)continue;
        int row=0;for(auto p:node["properties"].toArray()) {auto property=p.toObject();QString original;for(auto q:baseNode["properties"].toArray())if(q.toObject()["property"]==property["property"])original=q.toObject()["valueLiteral"].toString();
            add(tr("Property %1 (JSON scalar)").arg(property["property"].toString()),QString("v2Property_%1").arg(row++),property["valueLiteral"].toString(),original,{{"kind","node-property"},{"sourceIndex",sourceIndex},{"property",property["property"]}},true);
        }
        for(auto p:node["ports"].toArray()) {auto port=p.toObject();QString original;for(auto q:baseNode["ports"].toArray())if(q.toObject()["portIndex"]==port["portIndex"]&&q.toObject()["portId"]==port["portId"])original=q.toObject()["value"].toString();
            add(tr("Port %1 display name").arg(port["portId"].toString()),QString("v2Port_%1").arg(port["portIndex"].toInt()),port["value"].toString(),original,{{"kind","port-name"},{"sourceIndex",sourceIndex},{"portIndex",port["portIndex"]},{"portId",port["portId"]}},false);
        }
    }
    m_error=new QLabel(this);m_error->setObjectName("v2EditError");m_error->setWordWrap(true);m_error->setTextInteractionFlags(Qt::TextSelectableByMouse);form->addRow(m_error);
    auto *buttons=new QDialogButtonBox(this);m_apply=buttons->addButton(tr("Apply preview (not saved)"),QDialogButtonBox::ActionRole);m_apply->setObjectName("v2ApplyPreviewButton");buttons->addButton(tr("Cancel"),QDialogButtonBox::RejectRole);form->addRow(buttons);
    connect(m_apply,&QPushButton::clicked,this,&BlueprintV2EditDialog::previewRequested);connect(buttons,&QDialogButtonBox::rejected,this,&BlueprintV2EditDialog::reject);resize(640,300);
}
QJsonArray BlueprintV2EditDialog::edits() const {
    QJsonArray batch=m_pending;
    for(const auto &field:m_fields) {
        QJsonArray next;for(auto edit:batch)if(key(edit.toObject())!=key(field.target))next.append(edit);batch=next;
        const auto value=field.input->text();if(field.literal?sameLiteral(value,field.original):value==field.original)continue;
        auto edit=field.target;edit[field.literal?"valueLiteral":"value"]=value;batch.append(edit);
    }
    return batch;
}
void BlueprintV2EditDialog::setPending(bool pending) {m_busy=pending;m_apply->setEnabled(!pending);for(auto &field:m_fields)field.input->setEnabled(!pending);m_error->setText(pending?tr("Validating preview…"):QString());}
void BlueprintV2EditDialog::showError(const QString &error) {setPending(false);m_error->setText(error);}
void BlueprintV2EditDialog::reject() {if(!m_busy)QDialog::reject();}
