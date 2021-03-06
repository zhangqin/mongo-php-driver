--TEST--
MongoCollection::find() MongoConnectionException no candidates due to RP
--SKIPIF--
<?php require_once "tests/utils/replicaset-failover.inc" ?>
--INI--
mongo.is_master_interval=1
--FILE--
<?php
require_once 'tests/utils/server.inc';

$server = new MongoShellServer();
$rs = $server->getReplicaSetConfig();
$mc = new MongoClient($rs['dsn'], array('replicaSet' => $rs['rsname']));

$c = $mc->selectCollection(dbname(), 'mongocollection-find_error-001');
$c->insert(array('x' => 1), array('w' => 'majority'));

// Disable secondaries so query has no candidates
$server->setMaintenanceForSecondaries(true);
sleep(3);

try {
    $c->setReadPreference(MongoClient::RP_SECONDARY);
    $document = $c->findOne(array('x' => 1), array('_id' => 0));
    var_dump($document);
} catch (MongoConnectionException $e) {
    var_dump($e->getMessage(), $e->getCode());
}

// Enable secondaries so query can succeed
$server->setMaintenanceForSecondaries(false);
sleep(3);

try {
    $c->setReadPreference(MongoClient::RP_SECONDARY);
    $document = $c->findOne(array('x' => 1), array('_id' => 0));
    var_dump($document);
} catch (MongoConnectionException $e) {
    var_dump($e->getMessage(), $e->getCode());
}

?>
--CLEAN--
<?php require_once "tests/utils/fix-secondaries.inc"; ?>
--EXPECTF--
string(26) "No candidate servers found"
int(71)
array(1) {
  ["x"]=>
  int(1)
}
