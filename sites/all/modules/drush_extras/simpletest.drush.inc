<?php
// $Id: simpletest.drush.inc,v 1.2.2.1 2008/12/31 04:49:53 weitzman Exp $

/**
 * @file Drush simpletest commands
 */

/**
 * Implementation of hook_drush_help().
 */
function simpletest_drush_help($section) {
  switch ($section) {
    case 'drush:test run':
      return dt("Usage drush [options] test run<classes>.\n\nRun the specified specified unit tests. If <classes> is omitted, all tests are run. <classes> should be a list of classes separated by a comma. For example: PageCreationTest,PageViewTest.");
    case 'drush:test list':
      return dt("Usage drush [options] test list.\n\nList the available tests. Use drush test run command to run them.");
  }
}

/**
 * Implementation of hook_drush_command().
 */
function simpletest_drush_command() {
  $items['test run'] = array(
    'callback' => 'drush_test_run',
    'description' => 'Run one or more Simpletest tests.',
  );
  $items['test list'] = array(
    'callback' => 'drush_test_list',
    'description' => 'List the available Simpletest test classes.',
  );
  return $items;
}

function drush_test_list() {
  simpletest_load();
  // TODO: Refactor simpletest.module so we don't copy code from DrupalUnitTests
  $files = array();
  foreach (module_list() as $module) {
    $module_path = drupal_get_path('module', $module);
    if (file_exists($module_path .'/tests/')) {
      $dir = $module_path .'/tests';
      $tests = file_scan_directory($dir, '\.test$');
      $files = array_merge($files, $tests);
    }
  }
  $files = array_keys($files);

  $existing_classes = get_declared_classes();
  foreach ($files as $file) {
    include_once($file);
  }
  $classes = array_diff(get_declared_classes(), $existing_classes);
  $rows[] = array(dt('Class'), dt('Name'), dt('Description'));
  foreach ($classes as $class) {
    $tmp = new $class;
    if (method_exists($tmp, 'get_info')) {
      $info =  $tmp->get_info();
      $rows[] = array($class, $info['name'], truncate_utf8($info['desc'], 30, TRUE, TRUE));
    }
  }
  return drush_print_table($rows, 0, TRUE);
}


function drush_test_run($tests = NULL) {
  if (is_null($tests)) {
    drush_print(dt('Running all tests'));
    $result = simpletest_run_tests();
  }
  else {
    $tests = explode(',', $tests);
    $result = simpletest_run_tests($tests, 'text');
  }
  return $result;
}