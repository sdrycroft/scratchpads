<?php

function femail_drush_command() {
  return array(
    'femail' => array(
      'callback' => 'femail_drush_callback',
      'description' => 'Drush command for importing mail messages into a forum'
    )
  );
}

function femail_drush_help($section) {
  switch ($section) {
    case 'drush:femail':
      return dt("Imports a mail message into a site's Forum.  Usually called by Procmail or similar");
  }
}

function _femail_get_msg(){
  $msg = array(
    'body' => fread(STDIN, 1000000)
  );
  $mailparse = mailparse_msg_create();
  mailparse_msg_parse($mailparse, $msg['body']);
  $structure = mailparse_msg_get_structure($mailparse);
  $body_is_set = FALSE;
  foreach ($structure as $s){
    // Get the subject to use as the node/comment title
    $part = mailparse_msg_get_part($mailparse, $s);
    $part_data = mailparse_msg_get_part_data($part);
    if(isset($part_data['headers']['subject'])){
      $msg['title'] = $part_data['headers']['subject'];
    }
    // Get the From: header, which will be used to work out which user to
    // attribute this node/comment to
    if(isset($part_data['headers']['from'])){
      $from = mailparse_rfc822_parse_addresses($part_data['headers']['from']);
      $msg['from'] = $from[0]['address'];
    }
    // Get the reply-to header, this will tell us whether or not we should
    // continue parsing this message
    if(isset($part_data['headers']['reply-to'])){
      $msg['reply-to'] = $part_data['headers']['reply-to'];
    }
    // Get the To: header, which will be used to work out which forum this 
    // message wants to go into
    if(isset($part_data['headers']['to'])){
      $to = mailparse_rfc822_parse_addresses($part_data['headers']['to']);
      $msg['to'] = $to[0]['address'];
    }
    // Get the body for the node/comment body
    if(!$body_is_set){
      if(isset($part_data['headers']['content-type'])){
        if(strpos($part_data['headers']['content-type'], 'text/plain') !== FALSE){
          if($part_data['headers']['content-transfer-encoding'] == 'base64'){
            $msg['body'] = base64_decode(substr($msg['body'], $part_data['starting-pos-body'], $part_data['ending-pos-body'] - $part_data['starting-pos-body']));
          } else {
            $msg['body'] = quoted_printable_decode(substr($msg['body'], $part_data['starting-pos-body'], $part_data['ending-pos-body'] - $part_data['starting-pos-body']));
          }
          $body_is_set = TRUE;
        }
      }
    }
    // Get the msgid
    if(isset($part_data['headers']['message-id'])){
      $msg['msgid'] = $part_data['headers']['message-id'];
    }
    // Get the InReplyTo header for possibly putting this message as a reply to 
    // another forum post
    if(isset($part_data['headers']['in-reply-to'])){
      $msg['in-reply-to'] = $part_data['headers']['in-reply-to'];
    }
  }
  return $msg;
}

function _femail_post_comment($msg, $user, $status){
  // First we'll make sure the in-reply-to is in the msgid table, if not, we
  // will have to add this as a new thread.
  if(isset($msg['in-reply-to'])){
    $result = db_query("SELECT cid, nid FROM {femail_msgs} WHERE msgid = '%s'", $msg['in-reply-to']);
  } else {
    // We need to select the nid and 0 as cid from node where title is the same
    // as the subject line (less "re:")
    $result = db_query("SELECT 0 AS cid, nid FROM node WHERE title = '%s' AND type = 'forum' LIMIT 1", trim(substr($msg['title'], 3)));
  }
  $row = db_fetch_array($result);
  if($row){
    // Check we have permission to post a forum post.
    if(user_access('post comments')){
      // Get the tid of the forum that this post will go into
      $edit = array(
        'author' => $user->name,
        'subject' => $msg['title'],
        'comment' => $msg['body'],
        'pid' => $row['cid'],
        'nid' => $row['nid'],
        'uid' => $user->uid,
        'op' => 'Save',
        'date' => 'now',
        'name' => $user->name,
        'status' => $status,
        'format' => variable_get('femail_input_format', NULL)
      );
      $cid = comment_save($edit);
      watchdog('femail','Posted forum comment: '.l($msg['title'], 'node/'.$row['nid'], array('fragment', 'comment-'.$cid)));
    } else {
      watchdog('femail','Failed to post forum comment: '.$msg['title']);
    }
    return TRUE;
  }
  watchdog('femail','Failed to create comment for "@subject" FROM "@email"', array('@subject' => $msg['title'], '@email' => $msg['from']), WATCHDOG_INFO);
  return FALSE;
}

function _femail_post_forum_topic($msg, $user, $status){
  // Get the tid of the forum that this post will go into
  $emails = variable_get('femail_emails', array());
  $tid = array_search($msg['to'], $emails);
  $term = taxonomy_get_term($tid);
  $node = new stdClass();
  $node->uid = $user->uid;
  $node->body = $msg['body'];
  $node->comment = variable_get('comment_forum', COMMENT_NODE_READ_WRITE);
  $node->title = $msg['title'];
  $node->type = 'forum';
  $node->taxonomy = array($term->vid => $tid);
  $node->status = $status;
  $node->format = variable_get('femail_input_format', NULL);
  node_save($node);
  watchdog('femail','Posted forum topic: '.l($node->title,'node/'.$node->nid), array(), WATCHDOG_INFO);
}

function femail_drush_callback(){
  $msg = _femail_get_msg();
  
  // Check to see this isn't a message that we've just sent out
  if($msg['reply-to'] == $msg['to']){
    return;
  }
  
  watchdog('femail', "Following message was received:\n\n".print_r($msg, TRUE), array(), WATCHDOG_INFO);

  // User needed for both comments, and nodes, so load first.
  global $user;
  $account = user_load(array('mail' => $msg['from']));
  if($account){
    $user = $account;
  } else {
    $row = db_fetch_array(db_query("SELECT uid FROM {femail_user_emails} WHERE email = '%s' AND status !=0", $msg['from']));
    if(is_array($row)){
      $user = user_load(array('uid' => array_pop($row)));
    } else {
      $user = user_load(array('uid' => 0));
    }
  }
  if(!user_access('post by femail')){
    // Add a message to the logs so that we know we received a message and that
    // it hasn't been published
    watchdog('femail','Message "@subject" FROM "@email" has been posted, but unpublished', array('@subject' => $msg['title'], '@email' => $msg['from']), WATCHDOG_INFO);
    $comment_status = 1;
    $node_status = 0;
  } else {
    $comment_status = 0;
    $node_status = 1;
  }
  // If the in-reply-to is set, then we need to create a comment else we need to
  // create a node, or creating a comment has failed as the msgid could not be
  // found.
  if(isset($msg['in-reply-to']) || strtolower(substr($msg['title'],0,3))=='re:'){
    if(_femail_post_comment($msg, $user, $comment_status)){
      return;
    }
  }
  // else we need to create a node, or creating a comment has failed as the 
  // msgid could not be found.
  _femail_post_forum_topic($msg, $user, $node_status);
}