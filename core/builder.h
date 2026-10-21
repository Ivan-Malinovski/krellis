// Authoring helper: a readable way to describe an initial layout (port of Trellis model/builder).
#pragma once

#include "document.h"

#include <stdexcept>

namespace trellis {

struct LayoutSpec {
    enum class Kind { View, Panel, Split, Stage };
    Kind kind = Kind::View;
    std::string id;
    // View
    std::string type, title;
    // Panel
    std::vector<LayoutSpec> views;
    int selected = 0;
    // Split
    Axis axis = Axis::X;
    std::vector<LayoutSpec> children;
    std::vector<double> weights;
    // Stage
    std::shared_ptr<LayoutSpec> child;
};

namespace layout {
LayoutSpec view(std::string type, std::string id = {}, std::string title = {});
LayoutSpec panel(std::vector<LayoutSpec> views, std::string id = {}, int selected = 0);
LayoutSpec split(Axis axis, std::vector<LayoutSpec> children, std::vector<double> weights = {}, std::string id = {});
LayoutSpec row(std::vector<LayoutSpec> children, std::vector<double> weights = {});
LayoutSpec column(std::vector<LayoutSpec> children, std::vector<double> weights = {});
LayoutSpec stage(const LayoutSpec *child = nullptr, std::string id = "stage");
} // namespace layout

struct FloatSpec {
    LayoutSpec panel;
    Rect rect;
    FloatingLayer layer = FloatingLayer::Overlay;
};

/// Compile a layout description into a document. Throws std::runtime_error on invalid specs.
LayoutDocument createDocument(const LayoutSpec *root, const std::vector<FloatSpec> &floating = {});

} // namespace trellis
