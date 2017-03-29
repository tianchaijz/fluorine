#include <iostream>

#include "rapidjson/writer.h"
#include <rapidjson/document.h>
#include "rapidjson/stringbuffer.h"

using namespace rapidjson;
using namespace std;

void testJson() {
  Document d;
  d.SetObject();

  Value array(kArrayType);
  Document::AllocatorType &allocator = d.GetAllocator();
  array.PushBack("hello", allocator).PushBack("world", allocator);
  d.AddMember("array", array, allocator);
  d.AddMember("number", 2, allocator);
  d.AddMember("hello", "world", allocator);

  rapidjson::Value object(rapidjson::kObjectType);
  object.AddMember("hello", "world", allocator);
  d.AddMember("object", object, allocator);
  d["object"]["hello"] = "world";

  StringBuffer buffer;
  Writer<rapidjson::StringBuffer> writer(buffer);
  d.Accept(writer);

  string a("a"), b("b");
  Value va, vb;
  va.SetString(a.c_str(), allocator);
  vb.SetString(b.c_str(), allocator);
  d.AddMember(va, vb, allocator);

  std::cout << buffer.GetString() << std::endl;
}

int main() {
  testJson();
  return 0;
}
