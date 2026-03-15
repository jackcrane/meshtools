#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "meshtools/app/EditorDocumentState.h"
#include "meshtools/mesh/MeshDocument.h"
#include "meshtools/ui/EditorUiTypes.h"

namespace meshtools::app {

struct DocumentHistorySnapshot {
    EditableDocumentState document_state;
    mesh::EntitySelection selection;
    std::optional<std::size_t> selected_entity_set_index;
};

struct DocumentHistoryRestoreTarget {
    std::size_t node_index = 0;
    std::size_t node_id = 0;
    std::string node_label;
    DocumentHistorySnapshot snapshot;
};

struct DocumentHistoryUiState {
    std::vector<ui::EditorUiState::HistoryEntry> entries;
    std::vector<ui::EditorUiState::HistoryBranchEntry> active_branch;
    std::size_t active_branch_position = 0;
    bool can_undo = false;
    bool can_redo = false;
};

class DocumentHistoryController {
  public:
    void clear();
    void reset(const mesh::MeshDocument* document, std::string root_label);
    void commit(
        const mesh::MeshDocument* document,
        std::string label,
        const mesh::EntitySelection& selection,
        std::optional<std::size_t> selected_entity_set_index
    );

    [[nodiscard]] std::optional<DocumentHistoryRestoreTarget> undoTarget() const;
    [[nodiscard]] std::optional<DocumentHistoryRestoreTarget> redoTarget() const;
    [[nodiscard]] std::optional<DocumentHistoryRestoreTarget> targetByNodeId(std::size_t node_id) const;
    void markRestored(std::size_t node_index);

    [[nodiscard]] DocumentHistoryUiState buildUiState() const;

  private:
    struct DocumentHistoryNode {
        std::size_t node_id = 0;
        std::optional<std::size_t> parent_index;
        std::vector<std::size_t> child_indices;
        std::optional<std::size_t> preferred_child_index;
        std::string label;
        DocumentHistorySnapshot snapshot;
    };

    [[nodiscard]] DocumentHistorySnapshot captureSnapshot(
        const mesh::MeshDocument* document,
        const mesh::EntitySelection& selection,
        std::optional<std::size_t> selected_entity_set_index
    ) const;
    [[nodiscard]] std::optional<std::size_t> nodeIndexById(std::size_t node_id) const;
    [[nodiscard]] std::optional<DocumentHistoryRestoreTarget> makeRestoreTarget(std::size_t node_index) const;
    void setPreferredPathToNode(std::size_t node_index);

    std::vector<DocumentHistoryNode> document_history_;
    std::optional<std::size_t> current_history_index_;
    std::size_t next_history_node_id_ = 1;
};

}  // namespace meshtools::app
