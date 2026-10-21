#include "session_store.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <cmath>
#include <set>
namespace trellis {
namespace {
QString str(const std::string &s) { return QString::fromStdString(s); }
QJsonArray rect(Rect r) { return {r.x,r.y,r.w,r.h}; }
QJsonArray ids(const std::vector<std::string> &values) { QJsonArray a; for (const auto &v : values) a.append(str(v)); return a; }
QJsonValue node(const NodePtr &n) {
    if (!n) return QJsonValue::Null;
    QJsonObject o{{"id",str(n->id)}};
    if (n->kind == Node::Kind::Panel) { o["kind"]="panel"; o["views"]=ids(n->views); }
    else if (n->kind == Node::Kind::Stage) { o["kind"]="stage"; o["child"]=node(n->child); }
    else {
        o["kind"]="split"; o["axis"]=n->axis == Axis::X ? "x" : "y";
        QJsonArray children, weights;
        for (const auto &c : n->children) children.append(node(c));
        for (double w : n->weights) weights.append(w);
        o["children"]=children; o["weights"]=weights;
    }
    return o;
}
struct Reader {
    bool valid = true;
    int nodes = 0;
    std::set<std::string> seen, views;
    std::string text(const QJsonValue &v, bool empty = false, int limit = 512) {
        if (!v.isString() || v.toString().size() > limit || (!empty && v.toString().isEmpty())) { valid=false; return {}; }
        return v.toString().toStdString();
    }
    std::vector<std::string> list(const QJsonValue &v) {
        std::vector<std::string> out;
        if (!v.isArray() || v.toArray().size() > 1024) { valid=false; return {}; }
        for (const auto &s : v.toArray()) out.push_back(text(s));
        return out;
    }
    double number(const QJsonValue &v, double min, double max) {
        const double d = v.toDouble(std::nan(""));
        if (!v.isDouble() || !std::isfinite(d) || d < min || d > max) { valid=false; return min; }
        return d;
    }
    int integer(const QJsonValue &v, int min, int max) {
        const double d = number(v, min, max);
        if (std::floor(d) != d) valid = false;
        return int(d);
    }
    Rect rectangle(const QJsonValue &v) {
        if (!v.isArray() || v.toArray().size()!=4) { valid=false; return {}; }
        const auto a=v.toArray();
        return {number(a[0],-100,100),number(a[1],-100,100),number(a[2],0.000001,100),number(a[3],0.000001,100)};
    }
    NodePtr tree(const QJsonValue &v, int depth=0) {
        if (v.isNull()) return nullptr;
        if (!v.isObject() || depth>64 || ++nodes>2048) { valid=false; return nullptr; }
        const auto o=v.toObject();
        const auto id=text(o["id"]);
        if (!seen.insert(id).second) { valid=false; return nullptr; }
        const auto kind=o["kind"].toString();
        if (kind=="panel") {
            const auto entries=list(o["views"]);
            if (entries.size()!=1 || !views.insert(entries.front()).second) { valid=false; return nullptr; }
            return makePanel(id,entries);
        }
        if (kind=="stage") return makeStage(id,tree(o["child"],depth+1));
        if (kind!="split" || !o["children"].isArray() || !o["weights"].isArray()) { valid=false; return nullptr; }
        const auto cs=o["children"].toArray(), ws=o["weights"].toArray();
        if (cs.isEmpty() || cs.size()!=ws.size() || cs.size()>1024 || (o["axis"]!="x" && o["axis"]!="y")) { valid=false; return nullptr; }
        std::vector<NodePtr> children; std::vector<double> weights;
        for (int i=0;i<cs.size();++i) { auto c=tree(cs[i],depth+1); if (!c) valid=false; children.push_back(c); weights.push_back(number(ws[i],0.000001,1e6)); }
        return makeSplit(id,o["axis"]=="x"?Axis::X:Axis::Y,children,weights);
    }
};
}
QByteArray encodeSession(const SessionSnapshot &session)
{
    QJsonArray canvases;
    for (const auto &canvas : session) {
        const auto &doc=canvas.document;
        QJsonArray floats,hidden,windows,framings;
        for (const auto &f : doc.floating) floats.append(QJsonObject{{"panel",node(f.panel)},{"rect",rect(f.rect)},{"z",f.z},{"layer",int(f.layer)}});
        for (const auto &h : doc.hidden) {
            const auto &r=h.restore;
            hidden.append(QJsonObject{{"panel",node(h.panel)},{"restore",QJsonObject{{"kind",int(r.kind)},{"rect",rect(r.rect.w>0&&r.rect.h>0?r.rect:Rect{0,0,1,1})},
                {"layer",int(r.layer)},{"beside",str(r.beside)},{"edge",int(r.edge)},{"share",r.share},{"panel",str(r.panel)}}}});
        }
        for (const auto &[panel,key] : canvas.windows) windows.append(QJsonObject{{"panel",str(panel)},{"app",str(key.app)},{"role",str(key.role)}});
        for (const auto &f : doc.navigation.framings) framings.append(QJsonObject{{"id",str(f.id)},{"name",str(f.name)},{"frame",ids(f.frame)}});
        canvases.append(QJsonObject{{"output",str(canvas.output)},{"root",node(doc.root)},{"floating",floats},{"hidden",hidden},{"windows",windows},
            {"navigation",QJsonObject{{"frame",ids(doc.navigation.frame)},{"framings",framings}}}});
    }
    return QJsonDocument(QJsonObject{{"schema",1},{"canvases",canvases}}).toJson(QJsonDocument::Indented);
}
std::optional<SessionSnapshot> decodeSession(const QByteArray &bytes, QString *error)
{
    auto fail=[error](const QString &message)->std::optional<SessionSnapshot>{ if(error)*error=message;return std::nullopt; };
    if(bytes.size()>2*1024*1024) return fail("Session exceeds 2 MiB");
    QJsonParseError parse;
    const auto json=QJsonDocument::fromJson(bytes,&parse);
    if(parse.error!=QJsonParseError::NoError || !json.isObject()) return fail("Invalid session JSON");
    const auto root=json.object();
    if(root["schema"]!=1 || !root["canvases"].isArray() || root["canvases"].toArray().size()>32) return fail("Unsupported session schema");
    SessionSnapshot result; std::set<std::string> outputs;
    for(const auto &value : root["canvases"].toArray()) {
        if(!value.isObject()) return fail("Invalid canvas");
        Reader r; SavedCanvas canvas; const auto o=value.toObject();
        canvas.output=r.text(o["output"]);
        if(!outputs.insert(canvas.output).second) r.valid=false;
        canvas.document.root=r.tree(o["root"]);
        if(!o["floating"].isArray() || !o["hidden"].isArray() || !o["windows"].isArray()) r.valid=false;
        for(const auto &value : o["floating"].toArray()) {
            const auto f=value.toObject(); const auto panel=r.tree(f["panel"]);
            if(!panel || panel->kind!=Node::Kind::Panel) r.valid=false;
            const int layer=r.integer(f["layer"],0,1);
            canvas.document.floating.push_back({panel,r.rectangle(f["rect"]),r.integer(f["z"],-10000,10000),FloatingLayer(layer)});
        }
        for(const auto &value : o["hidden"].toArray()) {
            const auto h=value.toObject(), restore=h["restore"].toObject();
            const auto panel=r.tree(h["panel"]); if(!panel || panel->kind!=Node::Kind::Panel) r.valid=false;
            RestoreTarget target;
            target.kind=RestoreTarget::Kind(r.integer(restore["kind"],0,2)); target.rect=r.rectangle(restore["rect"]);
            target.layer=FloatingLayer(r.integer(restore["layer"],0,1)); target.edge=Edge(r.integer(restore["edge"],0,3));
            target.share=r.number(restore["share"],0.000001,1);
            target.beside=r.text(restore["beside"],true);target.panel=r.text(restore["panel"],true);
            canvas.document.hidden.push_back({panel,target});
        }
        if (!r.valid) return fail("Invalid session tree");
        std::set<std::string> panels;
        for(const auto &p : allPanels(canvas.document)) if(p) panels.insert(p->id);
        for(const auto &value : o["windows"].toArray()) {
            const auto w=value.toObject(); const auto panel=r.text(w["panel"]);
            if(!panels.contains(panel) || canvas.windows.contains(panel)) r.valid=false;
            canvas.windows[panel]={r.text(w["app"],true),r.text(w["role"],true)};
        }
        if(canvas.windows.size()!=panels.size()) r.valid=false;
        const auto navigation=o["navigation"].toObject();
        canvas.document.navigation.frame=r.list(navigation["frame"]);
        if(!navigation["framings"].isArray() || navigation["framings"].toArray().size()>128) r.valid=false;
        std::set<std::string> framingIds;
        for(const auto &value : navigation["framings"].toArray()) {
            const auto f=value.toObject();const auto id=r.text(f["id"]); if(!framingIds.insert(id).second) r.valid=false;
            canvas.document.navigation.framings.push_back({id,r.text(f["name"],false,80),r.list(f["frame"])});
        }
        if(!r.valid) return fail("Invalid session structure");
        for(const auto &p : allPanels(canvas.document)) for(const auto &v : p->views) canvas.document.views[v]={"native",{}, {}};
        canvas.document=sanitize(std::move(canvas.document));
        result.push_back(std::move(canvas));
    }
    return result;
}
}
