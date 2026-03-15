#include "meshtools/app/DocumentHistoryController.h"

#include <algorithm>
#include <iostream>
#include <iterator>
#include <utility>

namespace meshtools::app {

void DocumentHistoryController::clear() {
    document_history_.clear();
    current_history_index_.reset();
    next_history_node_id_ = 1;
}

void DocumentHistoryController::reset(const mesh::MeshDocument* document, std::string root_label) {
    clear();

    if (document == nullptr) {
        return;
    }

    document_history_.push_back(DocumentHistoryNode{
        .node_id = next_history_node_id_++,
        .parent_index = std::nullopt,
        .child_indices = {},
        .preferred_child_index = std::nullopt,
        .label = std::move(root_label),
        .snapshot = captureSnapshot(document, mesh::EntitySelection{}, std::nullopt),
    });
    current_history_index_ = 0;
}

void DocumentHistoryController::commit(
    const mesh::MeshDocument* document,
    std::string label,
    const mesh::EntitySelection& selection,
    std::optional<std::size_t> selected_entity_set_index
) {
    if (document == nullptr) {
        return;
    }

    if (!current_history_index_.has_value()) {
        reset(document, "Current document");
    }

    const std::size_t parent_index = *current_history_index_;
    document_history_.push_back(DocumentHistoryNode{
        .node_id = next_history_node_id_++,
        .parent_index = parent_index,
        .child_indices = {},
        .preferred_child_index = std::nullopt,
        .label = std::move(label),
        .snapshot = captureSnapshot(document, selection, selected_entity_set_index),
    });

    const std::size_t new_index = document_history_.size() - 1U;
    document_history_[parent_index].child_indices.push_back(new_index);
    document_history_[parent_index].preferred_child_index = new_index;
    current_history_index_ = new_index;
    setPreferredPathToNode(new_index);
    std::cout
        << "[HISTORYDBG] commit node=r" << document_history_[new_index].node_id
        << " parent=r" << document_history_[parent_index].node_id
        << " label=\"" << document_history_[new_index].label << "\""
        << " mesh_revision=" << document_history_[new_index].snapshot.document_state.mesh_revision
        << std::endl;
}

std::optional<DocumentHistoryRestoreTarget> DocumentHistoryController::undoTarget() const {
    if (!current_history_index_.has_value()) {
        return std::nullopt;
    }

    const std::optional<std::size_t> parent_index = document_history_[*current_history_index_].parent_index;
    if (!parent_index.has_value()) {
        return std::nullopt;
    }

    return makeRestoreTarget(*parent_index);
}

std::optional<DocumentHistoryRestoreTarget> DocumentHistoryController::redoTarget() const {
    if (!current_history_index_.has_value()) {
        return std::nullopt;
    }

    const std::optional<std::size_t> child_index = document_history_[*current_history_index_].preferred_child_index;
    if (!child_index.has_value()) {
        return std::nullopt;
    }

    return makeRestoreTarget(*child_index);
}

std::optional<DocumentHistoryRestoreTarget> DocumentHistoryController::targetByNodeId(std::size_t node_id) const {
    const std::optional<std::size_t> node_index = nodeIndexById(node_id);
    if (!node_index.has_value()) {
        return std::nullopt;
    }

    return makeRestoreTarget(*node_index);
}

void DocumentHistoryController::markRestored(std::size_t node_index) {
    if (node_index >= document_history_.size()) {
        return;
    }

    current_history_index_ = node_index;
    setPreferredPathToNode(node_index);
    const DocumentHistorySnapshot& snapshot = document_history_[node_index].snapshot;
    std::cout
        << "[HISTORYDBG] restore node=r" << document_history_[node_index].node_id
        << " label=\"" << document_history_[node_index].label << "\""
        << " mesh_revision=" << snapshot.document_state.mesh_revision
        << " triangles=" << snapshot.document_state.triangles.size()
        << " vertices=" << snapshot.document_state.positions.size()
        << std::endl;
}

DocumentHistoryUiState DocumentHistoryController::buildUiState() const {
    DocumentHistoryUiState state;
    state.can_undo =
        current_history_index_.has_value() &&
        document_history_[*current_history_index_].parent_index.has_value();
    state.can_redo =
        current_history_index_.has_value() &&
        document_history_[*current_history_index_].preferred_child_index.has_value();

    if (document_history_.empty()) {
        return state;
    }

    std::vector<std::size_t> active_branch_indices;
    std::size_t branch_index = 0;
    while (branch_index < document_history_.size()) {
        active_branch_indices.push_back(branch_index);
        if (!document_history_[branch_index].preferred_child_index.has_value()) {
            break;
        }
        branch_index = *document_history_[branch_index].preferred_child_index;
    }

    for (std::size_t index : active_branch_indices) {
        state.active_branch.push_back(ui::EditorUiState::HistoryBranchEntry{
            .node_id = document_history_[index].node_id,
            .label = document_history_[index].label,
        });
    }
    if (current_history_index_.has_value()) {
        const auto current_match = std::find(active_branch_indices.begin(), active_branch_indices.end(), *current_history_index_);
        if (current_match != active_branch_indices.end()) {
            state.active_branch_position =
                static_cast<std::size_t>(std::distance(active_branch_indices.begin(), current_match));
        }
    }

    const auto append_entries = [&](const auto& self, std::size_t node_index, std::size_t depth) -> void {
        const DocumentHistoryNode& node = document_history_[node_index];
        const bool is_on_active_branch =
            std::find(active_branch_indices.begin(), active_branch_indices.end(), node_index) != active_branch_indices.end();
        state.entries.push_back(ui::EditorUiState::HistoryEntry{
            .node_id = node.node_id,
            .depth = depth,
            .label = node.label,
            .is_current = current_history_index_.has_value() && *current_history_index_ == node_index,
            .is_on_active_branch = is_on_active_branch,
            .has_children = !node.child_indices.empty(),
        });
        for (std::size_t child_index : node.child_indices) {
            self(self, child_index, depth + 1U);
        }
    };

    append_entries(append_entries, 0, 0);
    return state;
}

DocumentHistorySnapshot DocumentHistoryController::captureSnapshot(
    const mesh::MeshDocument* document,
    const mesh::EntitySelection& selection,
    std::optional<std::size_t> selected_entity_set_index
) const {
    DocumentHistorySnapshot snapshot{
        .document_state = document != nullptr ? makeEditableDocumentState(*document) : EditableDocumentState{},
        .selection = selection,
        .selected_entity_set_index = selected_entity_set_index,
    };
    if (snapshot.selected_entity_set_index.has_value() &&
        snapshot.selected_entity_set_index.value() >= snapshot.document_state.entity_sets.size()) {
        snapshot.selected_entity_set_index.reset();
    }
    return snapshot;
}

std::optional<std::size_t> DocumentHistoryController::nodeIndexById(std::size_t node_id) const {
    const auto match = std::find_if(
        document_history_.begin(),
        document_history_.end(),
        [node_id](const DocumentHistoryNode& node) {
            return node.node_id == node_id;
        }
    );
    if (match == document_history_.end()) {
        return std::nullopt;
    }

    return static_cast<std::size_t>(std::distance(document_history_.begin(), match));
}

std::optional<DocumentHistoryRestoreTarget> DocumentHistoryController::makeRestoreTarget(std::size_t node_index) const {
    if (node_index >= document_history_.size()) {
        return std::nullopt;
    }

    const DocumentHistoryNode& node = document_history_[node_index];
    return DocumentHistoryRestoreTarget{
        .node_index = node_index,
        .node_id = node.node_id,
        .node_label = node.label,
        .snapshot = node.snapshot,
    };
}

void DocumentHistoryController::setPreferredPathToNode(std::size_t node_index) {
    while (node_index < document_history_.size() && document_history_[node_index].parent_index.has_value()) {
        const std::size_t parent_index = *document_history_[node_index].parent_index;
        document_history_[parent_index].preferred_child_index = node_index;
        node_index = parent_index;
    }
}

}  // namespace meshtools::app
