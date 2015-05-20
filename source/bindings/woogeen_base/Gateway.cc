/*
 * Copyright 2014 Intel Corporation All Rights Reserved. 
 * 
 * The source code contained or described herein and all documents related to the 
 * source code ("Material") are owned by Intel Corporation or its suppliers or 
 * licensors. Title to the Material remains with Intel Corporation or its suppliers 
 * and licensors. The Material contains trade secrets and proprietary and 
 * confidential information of Intel or its suppliers and licensors. The Material 
 * is protected by worldwide copyright and trade secret laws and treaty provisions. 
 * No part of the Material may be used, copied, reproduced, modified, published, 
 * uploaded, posted, transmitted, distributed, or disclosed in any way without 
 * Intel's prior express written permission.
 * 
 * No license under any patent, copyright, trade secret or other intellectual 
 * property right is granted to or conferred upon you by disclosure or delivery of 
 * the Materials, either expressly, by implication, inducement, estoppel or 
 * otherwise. Any license under such intellectual property rights must be express 
 * and approved by Intel in writing.
 */

#ifndef BUILDING_NODE_EXTENSION
#define BUILDING_NODE_EXTENSION
#endif

#include "Gateway.h"

#include "NodeEventRegistry.h"

using namespace v8;

Gateway::Gateway() {};
Gateway::~Gateway() {};

void Gateway::Init(Handle<Object> target) {
  // Prepare constructor template
  Local<FunctionTemplate> tpl = FunctionTemplate::New(New);
  tpl->SetClassName(String::NewSymbol("Gateway"));
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  // Prototype
  tpl->PrototypeTemplate()->Set(String::NewSymbol("close"),
      FunctionTemplate::New(close)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("addPublisher"),
      FunctionTemplate::New(addPublisher)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("removePublisher"),
      FunctionTemplate::New(removePublisher)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("addSubscriber"),
      FunctionTemplate::New(addSubscriber)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("removeSubscriber"),
      FunctionTemplate::New(removeSubscriber)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("addExternalOutput"),
      FunctionTemplate::New(addExternalOutput)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("removeExternalOutput"),
      FunctionTemplate::New(removeExternalOutput)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("addExternalPublisher"),
      FunctionTemplate::New(addExternalPublisher)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("addEventListener"),
      FunctionTemplate::New(addEventListener)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("customMessage"),
      FunctionTemplate::New(customMessage)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("retrieveStatistics"),
      FunctionTemplate::New(retrieveStatistics)->GetFunction());

  tpl->PrototypeTemplate()->Set(String::NewSymbol("subscribeStream"),
      FunctionTemplate::New(subscribeStream)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("unsubscribeStream"),
      FunctionTemplate::New(unsubscribeStream)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("publishStream"),
      FunctionTemplate::New(publishStream)->GetFunction());
  tpl->PrototypeTemplate()->Set(String::NewSymbol("unpublishStream"),
      FunctionTemplate::New(unpublishStream)->GetFunction());

  Persistent<Function> constructor =
      Persistent<Function>::New(tpl->GetFunction());
  target->Set(String::NewSymbol("Gateway"), constructor);
}

Handle<Value> Gateway::New(const Arguments& args) {
  HandleScope scope;

  String::Utf8Value param(args[0]->ToString());
  std::string customParam = std::string(*param);

  Gateway* obj = new Gateway();
  obj->me = woogeen_base::Gateway::createGatewayInstance(customParam);

  obj->Wrap(args.This());

  return args.This();
}

Handle<Value> Gateway::close(const Arguments& args) {
  HandleScope scope;
  Gateway* obj = ObjectWrap::Unwrap<Gateway>(args.This());
  woogeen_base::Gateway* me = obj->me;

  me->destroyAsyncEvents();
  delete me;

  return scope.Close(Null());
}

Handle<Value> Gateway::addPublisher(const Arguments& args) {
  HandleScope scope;

  Gateway* obj = ObjectWrap::Unwrap<Gateway>(args.This());
  woogeen_base::Gateway* me = obj->me;

  WebRtcConnection* param0 =
      ObjectWrap::Unwrap<WebRtcConnection>(args[0]->ToObject());
  erizo::WebRtcConnection* wr = (erizo::WebRtcConnection*)param0->me;

  String::Utf8Value param1(args[1]->ToString());
  std::string clientId = std::string(*param1);

  String::Utf8Value param2(args[2]->ToString());
  std::string videoResolution = std::string(*param2);

  bool added = me->addPublisher(wr, clientId, videoResolution);

  return scope.Close(Boolean::New(added));
}

Handle<Value> Gateway::addExternalPublisher(const Arguments& args) {
  HandleScope scope;

  Gateway* obj = ObjectWrap::Unwrap<Gateway>(args.This());
  woogeen_base::Gateway* me = obj->me;

  ExternalInput* param = ObjectWrap::Unwrap<ExternalInput>(args[0]->ToObject());
  erizo::ExternalInput* wr = (erizo::ExternalInput*)param->me;

  String::Utf8Value param1(args[1]->ToString());
  std::string clientId = std::string(*param1);

  bool added = me->addPublisher(wr, clientId);

  return scope.Close(Boolean::New(added));
}

Handle<Value> Gateway::removePublisher(const Arguments& args) {
  HandleScope scope;

  Gateway* obj = ObjectWrap::Unwrap<Gateway>(args.This());
  woogeen_base::Gateway* me = obj->me;
  String::Utf8Value param(args[0]->ToString());
  std::string id = std::string(*param);

  me->removePublisher(id);

  return scope.Close(Null());
}

Handle<Value> Gateway::addSubscriber(const Arguments& args) {
  HandleScope scope;

  Gateway* obj = ObjectWrap::Unwrap<Gateway>(args.This());
  woogeen_base::Gateway* me = obj->me;

  WebRtcConnection* param =
      ObjectWrap::Unwrap<WebRtcConnection>(args[0]->ToObject());
  erizo::WebRtcConnection* wr = param->me;

  // get the param
  v8::String::Utf8Value param1(args[1]->ToString());

  // convert it to string
  std::string peerId = std::string(*param1);
  me->addSubscriber(wr, peerId);

  return scope.Close(Null());
}

Handle<Value> Gateway::removeSubscriber(const Arguments& args) {
  HandleScope scope;

  Gateway* obj = ObjectWrap::Unwrap<Gateway>(args.This());
  woogeen_base::Gateway* me = obj->me;

  // get the param
  v8::String::Utf8Value param(args[0]->ToString());

  // convert it to string
  std::string peerId = std::string(*param);
  me->removeSubscriber(peerId);

  return scope.Close(Null());
}

Handle<Value> Gateway::addExternalOutput(const Arguments& args) {
  HandleScope scope;

  Gateway* obj = ObjectWrap::Unwrap<Gateway>(args.This());
  woogeen_base::Gateway* me = obj->me;

  String::Utf8Value param(args[0]->ToString());
  std::string configParam = std::string(*param);

  bool succeeded = me->addExternalOutput(configParam);

  return scope.Close(Boolean::New(succeeded));
}

Handle<Value> Gateway::removeExternalOutput(const Arguments& args) {
  HandleScope scope;

  Gateway* obj = ObjectWrap::Unwrap<Gateway>(args.This());
  woogeen_base::Gateway* me = obj->me;

  // get the param
  v8::String::Utf8Value param(args[0]->ToString());

  // convert it to string
  std::string outputId = std::string(*param);
  bool succeeded = me->removeExternalOutput(outputId);

  return scope.Close(Boolean::New(succeeded));
}

Handle<Value> Gateway::addEventListener(const Arguments& args) {
  HandleScope scope;
  if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsFunction()) {
    return ThrowException(Exception::TypeError(String::New("Wrong arguments")));
  }

  // setup node event listener
  v8::String::Utf8Value str(args[0]->ToString());
  std::string key = std::string(*str);
  Persistent<Function> cb = Persistent<Function>::New(Local<Function>::Cast(args[1]));
  NodeEventRegistry* registry = new NodeEventRegistry(cb);
  // setup notification in core
  Gateway* obj = ObjectWrap::Unwrap<Gateway>(args.This());
  woogeen_base::Gateway* me = obj->me;
  me->setupAsyncEvent(key, registry);

  return scope.Close(Undefined());
}

Handle<Value> Gateway::customMessage(const Arguments& args) {
  HandleScope scope;
  if (args.Length() < 1 || !args[0]->IsString() ) {
    return ThrowException(Exception::TypeError(String::New("Wrong arguments")));
  }

  Gateway* obj = ObjectWrap::Unwrap<Gateway>(args.This());
  woogeen_base::Gateway* me = obj->me;
  v8::String::Utf8Value str(args[0]->ToString());
  std::string message = std::string(*str);
  me->customMessage(message);

  return scope.Close(Undefined());
}

Handle<Value> Gateway::retrieveStatistics(const Arguments& args) {
  HandleScope scope;

  Gateway* obj = ObjectWrap::Unwrap<Gateway>(args.This());
  woogeen_base::Gateway *me = obj->me;

  std::string stats = me->retrieveStatistics();

  return scope.Close(String::NewSymbol(stats.c_str()));
}

Handle<Value> Gateway::subscribeStream(const Arguments& args) {
  HandleScope scope;
  Gateway* obj = ObjectWrap::Unwrap<Gateway>(args.This());
  woogeen_base::Gateway* me = obj->me;
  v8::String::Utf8Value param(args[0]->ToString());
  std::string id = std::string(*param);
  bool isAudio = args[1]->BooleanValue();
  me->subscribeStream(id, isAudio);
  return scope.Close(Undefined());
}

Handle<Value> Gateway::unsubscribeStream(const Arguments& args) {
  HandleScope scope;
  Gateway* obj = ObjectWrap::Unwrap<Gateway>(args.This());
  woogeen_base::Gateway* me = obj->me;
  v8::String::Utf8Value param(args[0]->ToString());
  std::string id = std::string(*param);
  bool isAudio = args[1]->BooleanValue();
  me->unsubscribeStream(id, isAudio);
  return scope.Close(Undefined());
}

Handle<Value> Gateway::publishStream(const Arguments& args) {
  HandleScope scope;
  Gateway* obj = ObjectWrap::Unwrap<Gateway>(args.This());
  woogeen_base::Gateway* me = obj->me;
  v8::String::Utf8Value param(args[0]->ToString());
  std::string id = std::string(*param);
  bool isAudio = args[1]->BooleanValue();
  me->publishStream(id, isAudio);
  return scope.Close(Undefined());
}

Handle<Value> Gateway::unpublishStream(const Arguments& args) {
  HandleScope scope;
  Gateway* obj = ObjectWrap::Unwrap<Gateway>(args.This());
  woogeen_base::Gateway* me = obj->me;
  v8::String::Utf8Value param(args[0]->ToString());
  std::string id = std::string(*param);
  bool isAudio = args[1]->BooleanValue();
  me->unpublishStream(id, isAudio);
  return scope.Close(Undefined());
}
