/*
 * Souffle - A Datalog Compiler
 * Copyright (c) 2013, 2014, Oracle and/or its affiliates. All rights reserved
 * Licensed under the Universal Permissive License v 1.0 as shown at:
 * - https://opensource.org/licenses/UPL
 * - <souffle root>/licenses/SOUFFLE-UPL.txt
 */

/************************************************************************
 *
 * @file ReadStream.h
 *
 ***********************************************************************/

#pragma once

#include "RamTypes.h"
#include "RecordTable.h"
#include "SymbolTable.h"
#include "Util.h"
#include "json11.h"
#include <cctype>
#include <memory>
#include <string>
#include <vector>

namespace souffle {

using json11::Json;

class ReadStream {
protected:
    ReadStream(const std::map<std::string, std::string>& rwOperation, SymbolTable& symbolTable,
            RecordTable& recordTable)
            : symbolTable(symbolTable), recordTable(recordTable) {
        const std::string& relationName{rwOperation.at("name")};

        std::string parseErrors;

        types = Json::parse(rwOperation.at("types"), parseErrors);

        assert(parseErrors.size() == 0 && "Internal JSON parsing failed.");

        arity = static_cast<size_t>(types[relationName]["arity"].long_value());
        auxiliaryArity = static_cast<size_t>(types[relationName]["auxArity"].long_value());

        for (size_t i = 0; i < arity + auxiliaryArity; ++i) {
            std::string type = types[relationName]["types"][i].string_value();
            typeAttributes.push_back(std::move(type));
        }
    }

public:
    template <typename T>
    void readAll(T& relation) {
        auto lease = symbolTable.acquireLock();
        (void)lease;
        while (const auto next = readNextTuple()) {
            const RamDomain* ramDomain = next.get();
            relation.insert(ramDomain);
        }
    }

    virtual ~ReadStream() = default;

protected:
    /**
     * Read a record from a string.
     *
     * @param source - string containing a record
     * @param recordTypeName - record type.
     * @parem pos - start parsing from this position.
     * @param consumed - if not nullptr: number of characters read.
     *
     */
    RamDomain readRecord(const std::string& source, const std::string& recordTypeName, size_t pos = 0,
            size_t* charactersRead = nullptr) {
        const size_t initial_position = pos;

        Json recordInfo = types["records"][recordTypeName];

        // Check if record type information are present
        if (recordInfo.is_null()) {
            throw std::invalid_argument("Missing record type information: " + recordTypeName);
        }

        // Handle nil case
        consumeWhiteSpace(source, pos);
        if (source.substr(pos, 3) == "nil") {
            if (charactersRead != nullptr) {
                *charactersRead = 3;
            }
            return 0;
        }

        const Json recordTypes = recordInfo["types"];
        const size_t recordArity = recordInfo["arity"].long_value();

        std::vector<RamDomain> recordValues(recordArity);

        consumeChar(source, '[', pos);

        for (size_t i = 0; i < recordArity; ++i) {
            const std::string& recordType = recordTypes[i].string_value();
            size_t consumed = 0;

            if (i > 0) {
                consumeChar(source, ',', pos);
            }
            consumeWhiteSpace(source, pos);
            switch (recordType[0]) {
                case 's':
                    recordValues[i] = readStringInRecord(source, pos, &consumed);
                    break;
                case 'i':
                    recordValues[i] = RamSignedFromString(source.substr(pos), &consumed);
                    break;
                case 'u':
                    recordValues[i] = ramBitCast(RamUnsignedFromString(source.substr(pos), &consumed));
                    break;
                case 'f':
                    recordValues[i] = ramBitCast(RamFloatFromString(source.substr(pos), &consumed));
                    break;
                case 'r':
                    recordValues[i] = readRecord(source, recordType, pos, &consumed);
                    break;
                default:
                    assert(false && "Invalid type attribute");
            }
            pos += consumed;
        }
        consumeChar(source, ']', pos);

        if (charactersRead != nullptr) {
            *charactersRead = pos - initial_position;
        }

        return recordTable.pack(recordValues.data(), recordValues.size());
    }

    RamDomain readStringInRecord(const std::string& source, const size_t pos, size_t* charactersRead) {
        size_t endOfSymbol = source.find_first_of(",]", pos);

        if (endOfSymbol == std::string::npos) {
            throw std::invalid_argument("Unexpected end of input in record");
        }

        *charactersRead = endOfSymbol - pos;
        std::string str = source.substr(pos, *charactersRead);

        return symbolTable.unsafeLookup(str);
    }

    /**
     * Read past given character, consuming any preceding whitespace.
     */
    void consumeChar(const std::string& str, char c, size_t& pos) {
        consumeWhiteSpace(str, pos);
        if (pos >= str.length()) {
            throw std::invalid_argument("Unexpected end of input in record");
        }
        if (str[pos] != c) {
            std::stringstream error;
            error << "Expected: \'" << c << "\', got: " << str[pos];
            throw std::invalid_argument(error.str());
        }
        ++pos;
    }

    /**
     * Advance position in the string until first non-whitespace character.
     */
    void consumeWhiteSpace(const std::string& str, size_t& pos) {
        while (pos < str.length() && std::isspace(static_cast<unsigned char>(str[pos]))) {
            ++pos;
        }
    }

    Json types;

    virtual std::unique_ptr<RamDomain[]> readNextTuple() = 0;
    std::vector<std::string> typeAttributes;
    SymbolTable& symbolTable;
    RecordTable& recordTable;

    size_t arity;
    size_t auxiliaryArity;
};

class ReadStreamFactory {
public:
    virtual std::unique_ptr<ReadStream> getReader(
            const std::map<std::string, std::string>&, SymbolTable&, RecordTable&) = 0;
    virtual const std::string& getName() const = 0;
    virtual ~ReadStreamFactory() = default;
};

} /* namespace souffle */
