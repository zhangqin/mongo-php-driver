/**
 *  Copyright 2009-2013 10gen, Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */
#include <php.h>
#include <zend_exceptions.h>
#include "../php_mongo.h"
#include "../mongoclient.h"
#include "../db.h"
#include "../collection.h"
#include "db_ref.h"

extern zend_class_entry *mongo_ce_DB, *mongo_ce_Id, *mongo_ce_Exception;

zend_class_entry *mongo_ce_DBRef = NULL;

/* {{{ MongoDBRef::create()
 *
 * DB refs are of the form:
 * array( '$ref' => <collection>, '$id' => <id>[, $db => <dbname>] ) */
PHP_METHOD(MongoDBRef, create)
{
	char *ns, *db = NULL;
	int ns_len, db_len = 0;
	zval *zid, *retval;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sz|s", &ns, &ns_len, &zid, &db, &db_len) == FAILURE) {
		return;
	}

	retval = php_mongo_dbref_create(zid, ns, db TSRMLS_CC);
	RETURN_ZVAL(retval, 0, 1);
}

zval *php_mongo_dbref_create(zval *zid, char *ns, char *db TSRMLS_DC)
{
	zval *retval;

	if (Z_TYPE_P(zid) == IS_ARRAY || (Z_TYPE_P(zid) == IS_OBJECT && !instanceof_function(Z_OBJCE_P(zid), mongo_ce_Id TSRMLS_CC))) {
		zval **tmpval;

		if (zend_hash_find(HASH_P(zid), "_id", 4, (void**)&tmpval) != SUCCESS) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Cannot find _id key in the %s", zend_get_type_by_const(Z_TYPE_P(zid)));
			return NULL;
		}

		zid = *tmpval;
	} else if (Z_TYPE_P(zid) == IS_RESOURCE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Don't know what to do with a resource type");
		return NULL;
	}

	MAKE_STD_ZVAL(retval);
	array_init(retval);

	/* add collection name */
	add_assoc_string(retval, "$ref", ns, 1);

	/* add id field */
	add_assoc_zval(retval, "$id", zid);
	zval_add_ref(&zid);

	/* if we got a database name, add that, too */
	if (db) {
		add_assoc_string(retval, "$db", db, 1);
	}

	return retval;
}
/* }}} */

/* {{{ proto bool MongoDBRef::isRef(mixed ref)
   Checks if $ref has a $ref and $id property/key */
PHP_METHOD(MongoDBRef, isRef)
{
	zval *ref;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z", &ref) == FAILURE) {
		return;
	}

	if (IS_SCALAR_P(ref)) {
		RETURN_FALSE;
	}

	/* check that $ref and $id fields exists */
	if (zend_hash_exists(HASH_P(ref), "$ref", strlen("$ref") + 1) && zend_hash_exists(HASH_P(ref), "$id", strlen("$id") + 1)) {
		/* good enough */
		RETURN_TRUE;
	}

	RETURN_FALSE;
}
/* }}} */

/* {{{ MongoDBRef::get()
 */
PHP_METHOD(MongoDBRef, get)
{
	zval *zdb, *ref, *collection, *query;
	zval **ns, **id, **dbname;
	zend_bool alloced_db = 0;
	mongo_db *db;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Oz", &zdb, mongo_ce_DB, &ref) == FAILURE) {
		return;
	}
		
	PHP_MONGO_GET_DB(zdb);

	if (
		IS_SCALAR_P(ref) ||
		zend_hash_find(HASH_P(ref), "$ref", strlen("$ref") + 1, (void**)&ns) == FAILURE ||
		zend_hash_find(HASH_P(ref), "$id", strlen("$id") + 1, (void**)&id) == FAILURE
	) {
		RETURN_NULL();
	}

	if (Z_TYPE_PP(ns) != IS_STRING) {
		zend_throw_exception(mongo_ce_Exception, "MongoDBRef::get: $ref field must be a string", 10 TSRMLS_CC);
		return;
	}

	/* if this reference contains a db name, we have to switch dbs */
	if (zend_hash_find(HASH_P(ref), "$db", strlen("$db") + 1, (void**)&dbname) == SUCCESS) {
		/* just to be paranoid, make sure dbname is a string */
		if (Z_TYPE_PP(dbname) != IS_STRING) {
			zend_throw_exception(mongo_ce_Exception, "MongoDBRef::get: $db field must be a string", 11 TSRMLS_CC);
			return;
		}

		/* if the name in the $db field doesn't match the current db, make up
		 * a new db */
		if (strcmp(Z_STRVAL_PP(dbname), Z_STRVAL_P(db->name)) != 0) {
			zval *new_db_z;

			MAKE_STD_ZVAL(new_db_z);
			ZVAL_NULL(new_db_z);

			MONGO_METHOD1(MongoClient, selectDB, new_db_z, db->link, *dbname);

			/* make the new db the current one */
			zdb = new_db_z;

			/* so we can dtor this later */
			alloced_db = 1;
		}
	}

	/* get the collection */
	MAKE_STD_ZVAL(collection);
	MONGO_METHOD1(MongoDB, selectCollection, collection, zdb, *ns);

	/* query for the $id */
	MAKE_STD_ZVAL(query);
	array_init(query);
	add_assoc_zval(query, "_id", *id);
	zval_add_ref(id);

	/* return whatever's there */
	MONGO_METHOD1(MongoCollection, findOne, return_value, collection, query);

	/* cleanup */
	zval_ptr_dtor(&collection);
	zval_ptr_dtor(&query);
	if (alloced_db) {
		zval_ptr_dtor(&zdb);
	}
}
/* }}} */

static zend_function_entry MongoDBRef_methods[] = {
	PHP_ME(MongoDBRef, create, NULL, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	PHP_ME(MongoDBRef, isRef, NULL, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	PHP_ME(MongoDBRef, get, NULL, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	{ NULL, NULL, NULL }
};


void mongo_init_MongoDBRef(TSRMLS_D)
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "MongoDBRef", MongoDBRef_methods);
	mongo_ce_DBRef = zend_register_internal_class(&ce TSRMLS_CC);
}

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: fdm=marker
 * vim: noet sw=4 ts=4
 */
