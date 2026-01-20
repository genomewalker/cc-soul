#pragma once
// GraphApi: Triplet storage using mmap'd graph store
//
// Simple interface for storing and querying subject-predicate-object triplets.

#include "../mmap_graph_store.hpp"
#include "../types.hpp"
#include <string>
#include <vector>
#include <tuple>
#include <iostream>
#include <sys/stat.h>

namespace chitta {

class GraphApi {
public:
    GraphApi() = default;

    bool open(const std::string& base_path) {
        base_path_ = base_path;

        // Check if all required files exist for a complete graph store
        struct stat st;
        bool has_header = (stat((base_path + ".graph").c_str(), &st) == 0 && st.st_size > 0);
        bool has_triplets = (stat((base_path + ".triplets").c_str(), &st) == 0);
        bool has_subj_csr = (stat((base_path + ".subj_csr").c_str(), &st) == 0);
        bool has_obj_csr = (stat((base_path + ".obj_csr").c_str(), &st) == 0);

        // All files must exist for a valid store
        if (has_header && has_triplets && has_subj_csr && has_obj_csr) {
            if (store_.open(base_path)) {
                return true;
            }
            std::cerr << "[GraphApi] Failed to open existing store, recreating\n";
            store_.close();
        }

        // Create fresh store
        if (!store_.create(base_path)) {
            std::cerr << "[GraphApi] Failed to create graph store\n";
            return false;
        }

        return true;
    }

    void close() {
        store_.build_indices();  // Build CSR indices before closing
        store_.sync();
        store_.close();
    }

    void sync() {
        store_.build_indices();  // Rebuild CSR before sync
        store_.sync();
    }

    // Add single triplet
    bool add(const std::string& subject, const std::string& predicate,
             const std::string& object, float weight = 1.0f) {
        return store_.add(subject, predicate, object, weight);
    }

    // Batch add triplets
    size_t add_batch(const std::vector<std::tuple<std::string, std::string, std::string, float>>& triplets) {
        size_t added = store_.add_batch(triplets);
        store_.build_indices();
        return added;
    }

    // Query by subject: returns [(predicate, object, weight), ...]
    std::vector<std::tuple<std::string, std::string, float>>
    query_subject(const std::string& subject) const {
        return store_.query_subject(subject);
    }

    // Query by object: returns [(subject, predicate, weight), ...]
    std::vector<std::tuple<std::string, std::string, float>>
    query_object(const std::string& object) const {
        return store_.query_object(object);
    }

    // Query by predicate: returns [(subject, object, weight), ...]
    std::vector<std::tuple<std::string, std::string, float>>
    query_predicate(const std::string& predicate) const {
        return store_.query_predicate(predicate);
    }

    // Build CSR indices (call after bulk loading)
    void build_indices() {
        store_.build_indices();
    }

    // Stats
    size_t entity_count() const { return store_.entity_count(); }
    size_t predicate_count() const { return store_.predicate_count(); }
    size_t triplet_count() const { return store_.triplet_count(); }

    // Access underlying store for advanced operations
    MmapGraphStore& store() { return store_; }
    const MmapGraphStore& store() const { return store_; }

private:
    std::string base_path_;
    MmapGraphStore store_;
};

}  // namespace chitta
