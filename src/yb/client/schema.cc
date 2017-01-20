// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "yb/client/schema.h"

#include <unordered_map>

#include <glog/logging.h>

#include "yb/client/schema-internal.h"
#include "yb/client/value-internal.h"
#include "yb/common/partial_row.h"
#include "yb/common/schema.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/substitute.h"

MAKE_ENUM_LIMITS(yb::client::YBColumnStorageAttributes::EncodingType,
                 yb::client::YBColumnStorageAttributes::AUTO_ENCODING,
                 yb::client::YBColumnStorageAttributes::RLE);

MAKE_ENUM_LIMITS(yb::client::YBColumnStorageAttributes::CompressionType,
                 yb::client::YBColumnStorageAttributes::DEFAULT_COMPRESSION,
                 yb::client::YBColumnStorageAttributes::ZLIB);

MAKE_ENUM_LIMITS(yb::client::YBColumnSchema::DataType,
                 yb::client::YBColumnSchema::INT8,
                 yb::client::YBColumnSchema::BOOL);

using std::unordered_map;
using std::vector;
using strings::Substitute;

namespace yb {
namespace client {

yb::EncodingType ToInternalEncodingType(YBColumnStorageAttributes::EncodingType type) {
  switch (type) {
    case YBColumnStorageAttributes::AUTO_ENCODING: return yb::AUTO_ENCODING;
    case YBColumnStorageAttributes::PLAIN_ENCODING: return yb::PLAIN_ENCODING;
    case YBColumnStorageAttributes::PREFIX_ENCODING: return yb::PREFIX_ENCODING;
    case YBColumnStorageAttributes::DICT_ENCODING: return yb::DICT_ENCODING;
    case YBColumnStorageAttributes::GROUP_VARINT: return yb::GROUP_VARINT;
    case YBColumnStorageAttributes::RLE: return yb::RLE;
    case YBColumnStorageAttributes::BIT_SHUFFLE: return yb::BIT_SHUFFLE;
    default: LOG(FATAL) << "Unexpected encoding type: " << type;
  }
}

YBColumnStorageAttributes::EncodingType FromInternalEncodingType(yb::EncodingType type) {
  switch (type) {
    case yb::AUTO_ENCODING: return YBColumnStorageAttributes::AUTO_ENCODING;
    case yb::PLAIN_ENCODING: return YBColumnStorageAttributes::PLAIN_ENCODING;
    case yb::PREFIX_ENCODING: return YBColumnStorageAttributes::PREFIX_ENCODING;
    case yb::DICT_ENCODING: return YBColumnStorageAttributes::DICT_ENCODING;
    case yb::GROUP_VARINT: return YBColumnStorageAttributes::GROUP_VARINT;
    case yb::RLE: return YBColumnStorageAttributes::RLE;
    case yb::BIT_SHUFFLE: return YBColumnStorageAttributes::BIT_SHUFFLE;
    default: LOG(FATAL) << "Unexpected internal encoding type: " << type;
  }
}

yb::CompressionType ToInternalCompressionType(YBColumnStorageAttributes::CompressionType type) {
  switch (type) {
    case YBColumnStorageAttributes::DEFAULT_COMPRESSION: return yb::DEFAULT_COMPRESSION;
    case YBColumnStorageAttributes::NO_COMPRESSION: return yb::NO_COMPRESSION;
    case YBColumnStorageAttributes::SNAPPY: return yb::SNAPPY;
    case YBColumnStorageAttributes::LZ4: return yb::LZ4;
    case YBColumnStorageAttributes::ZLIB: return yb::ZLIB;
    default: LOG(FATAL) << "Unexpected compression type" << type;
  }
}

YBColumnStorageAttributes::CompressionType FromInternalCompressionType(
    yb::CompressionType type) {
  switch (type) {
    case yb::DEFAULT_COMPRESSION: return YBColumnStorageAttributes::DEFAULT_COMPRESSION;
    case yb::NO_COMPRESSION: return YBColumnStorageAttributes::NO_COMPRESSION;
    case yb::SNAPPY: return YBColumnStorageAttributes::SNAPPY;
    case yb::LZ4: return YBColumnStorageAttributes::LZ4;
    case yb::ZLIB: return YBColumnStorageAttributes::ZLIB;
    default: LOG(FATAL) << "Unexpected internal compression type: " << type;
  }
}

yb::DataType ToInternalDataType(YBColumnSchema::DataType type) {
  switch (type) {
    case YBColumnSchema::INT8: return yb::INT8;
    case YBColumnSchema::INT16: return yb::INT16;
    case YBColumnSchema::INT32: return yb::INT32;
    case YBColumnSchema::INT64: return yb::INT64;
    case YBColumnSchema::TIMESTAMP: return yb::TIMESTAMP;
    case YBColumnSchema::FLOAT: return yb::FLOAT;
    case YBColumnSchema::DOUBLE: return yb::DOUBLE;
    case YBColumnSchema::STRING: return yb::STRING;
    case YBColumnSchema::BINARY: return yb::BINARY;
    case YBColumnSchema::BOOL: return yb::BOOL;
    default: LOG(FATAL) << "Unexpected data type: " << type;
  }
}

YBColumnSchema::DataType FromInternalDataType(yb::DataType type) {
  switch (type) {
    case yb::INT8: return YBColumnSchema::INT8;
    case yb::INT16: return YBColumnSchema::INT16;
    case yb::INT32: return YBColumnSchema::INT32;
    case yb::INT64: return YBColumnSchema::INT64;
    case yb::TIMESTAMP: return YBColumnSchema::TIMESTAMP;
    case yb::FLOAT: return YBColumnSchema::FLOAT;
    case yb::DOUBLE: return YBColumnSchema::DOUBLE;
    case yb::STRING: return YBColumnSchema::STRING;
    case yb::BINARY: return YBColumnSchema::BINARY;
    case yb::BOOL: return YBColumnSchema::BOOL;
    default: LOG(FATAL) << "Unexpected internal data type: " << type;
  }
}

////////////////////////////////////////////////////////////
// YBColumnSpec
////////////////////////////////////////////////////////////

YBColumnSpec::YBColumnSpec(const std::string& name)
  : data_(new Data(name)) {
}

YBColumnSpec::~YBColumnSpec() {
  delete data_;
}

YBColumnSpec* YBColumnSpec::Type(YBColumnSchema::DataType type) {
  data_->has_type = true;
  data_->type = type;
  return this;
}

YBColumnSpec* YBColumnSpec::Order(int32_t order) {
  data_->has_order = true;
  data_->order = order;
  return this;
}

YBColumnSpec* YBColumnSpec::Default(YBValue* v) {
  data_->has_default = true;
  delete data_->default_val;
  data_->default_val = v;
  return this;
}

YBColumnSpec* YBColumnSpec::Compression(
    YBColumnStorageAttributes::CompressionType compression) {
  data_->has_compression = true;
  data_->compression = compression;
  return this;
}

YBColumnSpec* YBColumnSpec::Encoding(
    YBColumnStorageAttributes::EncodingType encoding) {
  data_->has_encoding = true;
  data_->encoding = encoding;
  return this;
}

YBColumnSpec* YBColumnSpec::BlockSize(int32_t block_size) {
  data_->has_block_size = true;
  data_->block_size = block_size;
  return this;
}

YBColumnSpec* YBColumnSpec::PrimaryKey() {
  NotNull();
  data_->primary_key = true;
  return this;
}

YBColumnSpec* YBColumnSpec::HashPrimaryKey() {
  PrimaryKey();
  data_->hash_primary_key = true;
  return this;
}

YBColumnSpec* YBColumnSpec::NotNull() {
  data_->has_nullable = true;
  data_->nullable = false;
  return this;
}

YBColumnSpec* YBColumnSpec::Nullable() {
  data_->has_nullable = true;
  data_->nullable = true;
  return this;
}

YBColumnSpec* YBColumnSpec::RemoveDefault() {
  data_->remove_default = true;
  return this;
}

YBColumnSpec* YBColumnSpec::RenameTo(const std::string& new_name) {
  data_->has_rename_to = true;
  data_->rename_to = new_name;
  return this;
}

Status YBColumnSpec::ToColumnSchema(YBColumnSchema* col) const {
  // Verify that the user isn't trying to use any methods that
  // don't make sense for CREATE.
  if (data_->has_rename_to) {
    // TODO(KUDU-861): adjust these errors as this method will also be used for
    // ALTER TABLE ADD COLUMN support.
    return STATUS(NotSupported, "cannot rename a column during CreateTable",
                                data_->name);
  }
  if (data_->remove_default) {
    return STATUS(NotSupported, "cannot remove default during CreateTable",
                                data_->name);
  }

  if (!data_->has_type) {
    return STATUS(InvalidArgument, "no type provided for column", data_->name);
  }
  DataType internal_type = ToInternalDataType(data_->type);

  bool nullable = data_->has_nullable ? data_->nullable : true;

  void* default_val = nullptr;
  // TODO: distinguish between DEFAULT NULL and no default?
  if (data_->has_default) {
    RETURN_NOT_OK(data_->default_val->data_->CheckTypeAndGetPointer(
                      data_->name, internal_type, &default_val));
  }

  // Encoding and compression
  YBColumnStorageAttributes::EncodingType encoding =
    YBColumnStorageAttributes::AUTO_ENCODING;
  if (data_->has_encoding) {
    encoding = data_->encoding;
  }

  YBColumnStorageAttributes::CompressionType compression =
    YBColumnStorageAttributes::DEFAULT_COMPRESSION;
  if (data_->has_compression) {
    compression = data_->compression;
  }

  int32_t block_size = 0; // '0' signifies server-side default
  if (data_->has_block_size) {
    block_size = data_->block_size;
  }

  *col = YBColumnSchema(data_->name, data_->type, nullable, data_->hash_primary_key,
                        default_val, YBColumnStorageAttributes(encoding, compression, block_size));

  return Status::OK();
}


////////////////////////////////////////////////////////////
// YBSchemaBuilder
////////////////////////////////////////////////////////////

class YB_NO_EXPORT YBSchemaBuilder::Data {
 public:
  Data()
      : has_key_col_names(false),
        key_hash_col_count(0) {
  }

  ~Data() {
    // Rather than delete the specs here, we have to do it in
    // ~YBSchemaBuilder(), to avoid a circular dependency in the
    // headers declaring friend classes with nested classes.
  }

  // These members can be used to specify a subset of columns are primary or hash primary keys.
  // NOTE: "key_col_names" and "key_hash_col_count" are not used unless "has_key_col_names" is true.
  bool has_key_col_names;
  vector<string> key_col_names;
  int key_hash_col_count;

  vector<YBColumnSpec*> specs;
  TableProperties table_properties;
};

YBSchemaBuilder::YBSchemaBuilder()
  : data_(new Data()) {
}

YBSchemaBuilder::~YBSchemaBuilder() {
  for (YBColumnSpec* spec : data_->specs) {
    // Can't use STLDeleteElements because YBSchemaBuilder
    // is a friend of YBColumnSpec in order to access its destructor.
    // STLDeleteElements is a free function and therefore can't access it.
    delete spec;
  }
  delete data_;
}

YBColumnSpec* YBSchemaBuilder::AddColumn(const std::string& name) {
  auto c = new YBColumnSpec(name);
  data_->specs.push_back(c);
  return c;
}

YBSchemaBuilder* YBSchemaBuilder::SetPrimaryKey(
    const std::vector<std::string>& key_col_names,
    int key_hash_col_count) {
  data_->has_key_col_names = true;
  data_->key_col_names = key_col_names;
  data_->key_hash_col_count = key_hash_col_count;
  return this;
}

YBSchemaBuilder* YBSchemaBuilder::SetTableProperties(const TableProperties& table_properties) {
  data_->table_properties = table_properties;
  return this;
}

Status YBSchemaBuilder::Build(YBSchema* schema) {
  vector<YBColumnSchema> cols;
  cols.resize(data_->specs.size(), YBColumnSchema());
  for (int i = 0; i < cols.size(); i++) {
    RETURN_NOT_OK(data_->specs[i]->ToColumnSchema(&cols[i]));
  }

  int num_key_cols = 0;
  if (!data_->has_key_col_names) {
    // Change the API to allow specifying each column individually as part of a primary key.
    // Previously, we must pass an extra list of columns if the key is a compound of columns.
    //
    // Removing the following restriction from Kudu:
    //   If they didn't explicitly pass the column names for key,
    //   then they should have set it on exactly one column.
    bool has_order_error = false;
    bool reached_regular_column = false;
    bool reached_primary_column = false;
    for (int i = 0; i < cols.size(); i++) {
      if (data_->specs[i]->data_->hash_primary_key) {
        num_key_cols++;
        if (reached_primary_column || reached_regular_column) {
          has_order_error = true;
          break;
        }

      } else if (data_->specs[i]->data_->primary_key) {
        num_key_cols++;
        if (reached_regular_column) {
          has_order_error = true;
          break;
        }
        reached_primary_column = true;

      } else {
        reached_regular_column = true;
      }
    }

    if (num_key_cols <= 0) {
      return STATUS(InvalidArgument, "no primary key specified");
    }

    if (has_order_error) {
      return STATUS(InvalidArgument,
                    "The given columns in a schema must be ordered as hash primary key columns "
                    "then primary key columns and then regular columns");
    }

  } else {
    // Build a map from name to index of all of the columns.
    unordered_map<string, int> name_to_idx_map;
    int i = 0;
    for (YBColumnSpec* spec : data_->specs) {
      // If they did pass the key column names, then we should not have explicitly
      // set it on any columns.
      if (spec->data_->primary_key) {
        return STATUS(InvalidArgument, "primary key specified by both SetPrimaryKey() and on a "
                                       "specific column", spec->data_->name);
      }

      // Set the primary keys here to make sure the two different APIs for ColumnSpecs yield the
      // same result.
      if (i < data_->key_hash_col_count) {
        spec->HashPrimaryKey();
      } else {
        spec->PrimaryKey();
      }

      // If we have a duplicate column name, the Schema::Reset() will catch it later,
      // anyway.
      name_to_idx_map[spec->data_->name] = i++;
    }

    // Convert the key column names to a set of indexes.
    vector<int> key_col_indexes;
    for (const string& key_col_name : data_->key_col_names) {
      int idx;
      if (!FindCopy(name_to_idx_map, key_col_name, &idx)) {
        return STATUS(InvalidArgument, "primary key column not defined", key_col_name);
      }
      key_col_indexes.push_back(idx);
    }

    // Currently we require that the key columns be contiguous at the front
    // of the schema. We'll lift this restriction later -- hence the more
    // flexible user-facing API.
    for (int i = 0; i < key_col_indexes.size(); i++) {
      if (key_col_indexes[i] != i) {
        return STATUS(InvalidArgument, "primary key columns must be listed first in the schema",
                                       data_->key_col_names[i]);
      }
    }

    // Indicate the first "num_key_cols" are primary key.
    num_key_cols = key_col_indexes.size();
  }

  RETURN_NOT_OK(schema->Reset(cols, num_key_cols, data_->table_properties));

  return Status::OK();
}


////////////////////////////////////////////////////////////
// YBColumnSchema
////////////////////////////////////////////////////////////

std::string YBColumnSchema::DataTypeToString(DataType type) {
  return DataType_Name(ToInternalDataType(type));
}

YBColumnSchema::YBColumnSchema(const std::string &name,
                               DataType type,
                               bool is_nullable,
                               bool is_hash_key,
                               const void* default_value,
                               YBColumnStorageAttributes attributes) {
  ColumnStorageAttributes attr_private;
  attr_private.encoding = ToInternalEncodingType(attributes.encoding());
  attr_private.compression = ToInternalCompressionType(attributes.compression());
  col_ = new ColumnSchema(name, ToInternalDataType(type), is_nullable, is_hash_key,
                          default_value, default_value, attr_private);
}

YBColumnSchema::YBColumnSchema(const YBColumnSchema& other)
  : col_(nullptr) {
  CopyFrom(other);
}

YBColumnSchema::YBColumnSchema() : col_(nullptr) {
}

YBColumnSchema::~YBColumnSchema() {
  delete col_;
}

YBColumnSchema& YBColumnSchema::operator=(const YBColumnSchema& other) {
  if (&other != this) {
    CopyFrom(other);
  }
  return *this;
}

void YBColumnSchema::CopyFrom(const YBColumnSchema& other) {
  delete col_;
  if (other.col_) {
    col_ = new ColumnSchema(*other.col_);
  } else {
    col_ = nullptr;
  }
}

bool YBColumnSchema::Equals(const YBColumnSchema& other) const {
  return this == &other ||
    col_ == other.col_ ||
    (col_ != nullptr && col_->Equals(*other.col_, true));
}

const std::string& YBColumnSchema::name() const {
  return DCHECK_NOTNULL(col_)->name();
}

bool YBColumnSchema::is_nullable() const {
  return DCHECK_NOTNULL(col_)->is_nullable();
}

bool YBColumnSchema::is_hash_key() const {
  return DCHECK_NOTNULL(col_)->is_hash_key();
}

YBColumnSchema::DataType YBColumnSchema::type() const {
  return FromInternalDataType(DCHECK_NOTNULL(col_)->type_info()->type());
}


////////////////////////////////////////////////////////////
// YBSchema
////////////////////////////////////////////////////////////

YBSchema::YBSchema()
    : schema_(nullptr) {
}

YBSchema::YBSchema(const YBSchema& other)
    : schema_(nullptr) {
  CopyFrom(other);
}

YBSchema::YBSchema(const Schema& schema)
    : schema_(new Schema(schema)) {
}

YBSchema::~YBSchema() {
  delete schema_;
}

YBSchema& YBSchema::operator=(const YBSchema& other) {
  if (&other != this) {
    CopyFrom(other);
  }
  return *this;
}

void YBSchema::CopyFrom(const YBSchema& other) {
  delete schema_;
  schema_ = new Schema(*other.schema_);
}

Status YBSchema::Reset(const vector<YBColumnSchema>& columns, int key_columns,
                       const TableProperties& table_properties) {
  vector<ColumnSchema> cols_private;
  for (const YBColumnSchema& col : columns) {
    cols_private.push_back(*col.col_);
  }
  gscoped_ptr<Schema> new_schema(new Schema());
  RETURN_NOT_OK(new_schema->Reset(cols_private, key_columns, table_properties));

  delete schema_;
  schema_ = new_schema.release();
  return Status::OK();
}

bool YBSchema::Equals(const YBSchema& other) const {
  return this == &other ||
      (schema_ && other.schema_ && schema_->Equals(*other.schema_));
}

YBColumnSchema YBSchema::Column(size_t idx) const {
  ColumnSchema col(schema_->column(idx));
  YBColumnStorageAttributes attrs(FromInternalEncodingType(col.attributes().encoding),
                                    FromInternalCompressionType(col.attributes().compression));
  return YBColumnSchema(col.name(), FromInternalDataType(col.type_info()->type()),
                        col.is_nullable(), col.is_hash_key(), col.read_default_value(),
                        attrs);
}

YBColumnSchema YBSchema::ColumnById(int32_t column_id) const {
  return Column(schema_->find_column_by_id(yb::ColumnId(column_id)));
}

int32_t YBSchema::ColumnId(size_t idx) const {
  return schema_->column_id(idx);
}

YBPartialRow* YBSchema::NewRow() const {
  return new YBPartialRow(schema_);
}

size_t YBSchema::num_columns() const {
  return schema_->num_columns();
}

size_t YBSchema::num_key_columns() const {
  return schema_->num_key_columns();
}

size_t YBSchema::num_hash_key_columns() const {
  return schema_->num_hash_key_columns();
}

void YBSchema::GetPrimaryKeyColumnIndexes(vector<int>* indexes) const {
  indexes->clear();
  indexes->resize(num_key_columns());
  for (int i = 0; i < num_key_columns(); i++) {
    (*indexes)[i] = i;
  }
}

} // namespace client
} // namespace yb
