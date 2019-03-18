/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2017, The Souffle Developers. All rights reserved
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file RamProgram.h
 *
 * Defines a Program of a relational algebra query
 *
 ***********************************************************************/

#pragma once

#include "RamStatement.h"

namespace souffle {

class RamProgram : public RamNode {
private:
    /** Relations of RAM program */
    std::map<std::string, std::unique_ptr<RamRelation>> relations;

    /** Main program */
    std::unique_ptr<RamStatement> main;

    /** Subroutines for querying computed relations */
    std::map<std::string, std::unique_ptr<RamStatement>> subroutines;

public:
    RamProgram() : RamNode(RN_Program) {}
    RamProgram(std::unique_ptr<RamStatement> main) : RamNode(RN_Program), main(std::move(main)) {}

    /** Obtain child nodes */
    std::vector<const RamNode*> getChildNodes() const override {
        std::vector<const RamNode*> children;
        if (main != nullptr) {
            children = main->getChildNodes();
        }
        for (auto& r : relations) {
            children.push_back(r.second.get());
        }
        children.push_back(main.get());
        for (auto& s : subroutines) {
            children.push_back(s.second.get());
        }
        return children;
    }

    /** Print */
    void print(std::ostream& out) const override {
        out << "PROGRAM" << std::endl;
        out << "DECLARATION" << std::endl;
        for (const auto& rel : relations) {
            out << "\t";
            rel.second->print(out);
            out << std::endl;
        }
        out << "END DECLARATION" << std::endl;
        out << *main;
        out << std::endl;
        for (const auto& subroutine : subroutines) {
            out << std::endl << "SUBROUTINE " << subroutine.first << std::endl;
            out << *subroutine.second << std::endl;
            out << "END SUBROUTINE" << std::endl;
        }
        out << "END PROGRAM" << std::endl;
    }

    /** Set main program */
    void setMain(std::unique_ptr<RamStatement> stmt) {
        main = std::move(stmt);
    }

    /** Get main program */
    RamStatement* getMain() const {
        assert(main);
        return main.get();
    }

    /** Add relation */
    void addRelation(std::unique_ptr<RamRelation> rel) {
        relations.insert(std::make_pair(rel->getName(), std::move(rel)));
    }

    /** Get relation */
    const RamRelation* getRelation(const std::string& name) const {
        auto it = relations.find(name);
        if (it != relations.end()) {
            return it->second.get();
        } else {
            return nullptr;
        }
    }

    /** Add subroutine */
    void addSubroutine(std::string name, std::unique_ptr<RamStatement> subroutine) {
        subroutines.insert(std::make_pair(name, std::move(subroutine)));
    }

    /** Get subroutines */
    const std::map<std::string, RamStatement*> getSubroutines() const {
        std::map<std::string, RamStatement*> subroutineRefs;
        for (auto& s : subroutines) {
            subroutineRefs.insert({s.first, s.second.get()});
        }
        return subroutineRefs;
    }

    /** Get subroutine */
    const RamStatement& getSubroutine(const std::string& name) const {
        return *subroutines.at(name);
    }

    /** Create clone */
    RamProgram* clone() const override {
        typedef std::map<const RamRelation*, const RamRelation*> RefMapType;
        RefMapType refMap;
        RamProgram* res = new RamProgram(std::unique_ptr<RamStatement>(main->clone()));
        for (auto& cur : relations) {
            RamRelation* newRel = cur.second->clone();
            refMap[cur.second.get()] = newRel;
            res->addRelation(std::unique_ptr<RamRelation>(newRel));
        }
        for (auto& cur : subroutines) {
            res->addSubroutine(cur.first, std::unique_ptr<RamStatement>(cur.second->clone()));
        }
        // Rewrite relation references
        class RamRefRewriter : public RamNodeMapper {
            RefMapType& refMap;

        public:
            RamRefRewriter(RefMapType& rm) : refMap(rm) {}
            std::unique_ptr<RamNode> operator()(std::unique_ptr<RamNode> node) const override {
                if (const RamRelationReference* relRef = dynamic_cast<RamRelationReference*>(node.get())) {
                    const RamRelation* rel = refMap[relRef->get()];
                    assert(rel != nullptr && "dangling RAM relation reference");
                    return std::unique_ptr<RamRelationReference>(new RamRelationReference(rel));
                } else
                    return node;
            }
        } refRewriter(refMap);
        res->apply(refRewriter);
        return res;
    }

    /** Apply mapper */
    void apply(const RamNodeMapper& map) override {
        main = map(std::move(main));
        for (auto& cur : relations) {
            cur.second = map(std::move(cur.second));
        }
        for (auto& cur : subroutines) {
            cur.second = map(std::move(cur.second));
        }
    }

protected:
    /** Check equality */
    bool equal(const RamNode& node) const override {
        assert(nullptr != dynamic_cast<const RamProgram*>(&node));
        const auto& other = static_cast<const RamProgram&>(node);
        if (relations.size() != other.relations.size() || subroutines.size() != other.subroutines.size()) {
            return false;
        }
        for (auto& cur : subroutines) {
            if (other.getSubroutine(cur.first) != getSubroutine(cur.first)) {
                return false;
            }
        }
        for (auto& cur : relations) {
            if (other.getRelation(cur.first) != getRelation(cur.first)) {
                return false;
            }
        }
        return getMain() == other.getMain();
    }
};

}  // end of namespace souffle
