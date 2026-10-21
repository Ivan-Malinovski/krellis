#include "builder.h"

#include <map>
#include <set>

namespace trellis {
namespace layout {

LayoutSpec view(std::string type, std::string id, std::string title)
{
    LayoutSpec s;
    s.kind = LayoutSpec::Kind::View;
    s.type = std::move(type);
    s.id = std::move(id);
    s.title = std::move(title);
    return s;
}
LayoutSpec panel(std::vector<LayoutSpec> views, std::string id, int selected)
{
    LayoutSpec s;
    s.kind = LayoutSpec::Kind::Panel;
    s.views = std::move(views);
    s.id = std::move(id);
    s.selected = selected;
    return s;
}
LayoutSpec split(Axis axis, std::vector<LayoutSpec> children, std::vector<double> weights, std::string id)
{
    LayoutSpec s;
    s.kind = LayoutSpec::Kind::Split;
    s.axis = axis;
    s.children = std::move(children);
    s.weights = std::move(weights);
    s.id = std::move(id);
    return s;
}
LayoutSpec row(std::vector<LayoutSpec> children, std::vector<double> weights)
{
    return split(Axis::X, std::move(children), std::move(weights));
}
LayoutSpec column(std::vector<LayoutSpec> children, std::vector<double> weights)
{
    return split(Axis::Y, std::move(children), std::move(weights));
}
LayoutSpec stage(const LayoutSpec *child, std::string id)
{
    LayoutSpec s;
    s.kind = LayoutSpec::Kind::Stage;
    s.id = std::move(id);
    if (child)
        s.child = std::make_shared<LayoutSpec>(*child);
    return s;
}

} // namespace layout

LayoutDocument createDocument(const LayoutSpec *root, const std::vector<FloatSpec> &floating)
{
    LayoutDocument doc;
    std::set<std::string> explicitIds;
    std::map<std::string, int> counters;

    std::function<void(const LayoutSpec &)> collect = [&](const LayoutSpec &s) {
        switch (s.kind) {
        case LayoutSpec::Kind::View:
            if (!s.id.empty())
                explicitIds.insert(s.id);
            break;
        case LayoutSpec::Kind::Panel:
            for (const auto &v : s.views)
                collect(v);
            break;
        case LayoutSpec::Kind::Split:
            for (const auto &c : s.children)
                collect(c);
            break;
        case LayoutSpec::Kind::Stage:
            if (s.child)
                collect(*s.child);
            break;
        }
    };
    if (root)
        collect(*root);
    for (const auto &f : floating)
        collect(f.panel);

    auto nextId = [&](const std::string &type) {
        std::string id;
        do {
            id = type + "-" + std::to_string(++counters[type]);
        } while (explicitIds.count(id) || doc.views.count(id));
        return id;
    };
    auto addView = [&](const LayoutSpec &s) {
        const std::string id = s.id.empty() ? nextId(s.type) : s.id;
        if (doc.views.count(id))
            throw std::runtime_error("Trellis: duplicate view id \"" + id + "\"");
        ViewRecord rec;
        rec.type = s.type;
        rec.title = s.title;
        doc.views[id] = rec;
        return id;
    };
    auto makePanelFrom = [&](const LayoutSpec &s) {
        std::vector<LayoutSpec> specs = s.kind == LayoutSpec::Kind::View ? std::vector<LayoutSpec>{s} : s.views;
        std::vector<std::string> ids;
        for (const auto &v : specs)
            ids.push_back(addView(v));
        const int index = s.kind == LayoutSpec::Kind::Panel ? s.selected : 0;
        std::string selected = ids.empty() ? std::string() : ids[std::min<size_t>(index, ids.size() - 1)];
        return makePanel((s.kind == LayoutSpec::Kind::Panel && !s.id.empty()) ? s.id : uid("panel"), ids, selected);
    };
    int stages = 0;
    std::function<NodePtr(const LayoutSpec &)> build = [&](const LayoutSpec &s) -> NodePtr {
        switch (s.kind) {
        case LayoutSpec::Kind::View:
        case LayoutSpec::Kind::Panel:
            return makePanelFrom(s);
        case LayoutSpec::Kind::Stage: {
            if (stages++)
                throw std::runtime_error("Trellis: a layout may contain one stage");
            NodePtr child = s.child ? build(*s.child) : nullptr;
            if (child && child->kind == Node::Kind::Stage)
                throw std::runtime_error("Trellis: a stage cannot contain a stage");
            return makeStage(s.id.empty() ? "stage" : s.id, child);
        }
        case LayoutSpec::Kind::Split: {
            std::vector<NodePtr> children;
            for (const auto &c : s.children)
                children.push_back(build(c));
            std::vector<double> weights = s.weights.size() == children.size() ? s.weights
                                                                               : std::vector<double>(children.size(), 1.0);
            return makeSplit(s.id.empty() ? uid("split") : s.id, s.axis, children, weights);
        }
        }
        return nullptr;
    };
    doc.root = root ? normalize(build(*root)) : nullptr;
    int z = 0;
    for (const auto &f : floating)
        doc.floating.push_back({makePanelFrom(f.panel), f.rect, ++z, f.layer});
    return doc;
}

} // namespace trellis
