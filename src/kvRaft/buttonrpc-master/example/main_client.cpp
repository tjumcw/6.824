#include <string>
#include <iostream>
#include <ctime>
#include "../buttonrpc.hpp"

#include <unistd.h>  // use sleep


#define buttont_assert(exp) { \
	if (!(exp)) {\
		std::cout << "ERROR: "; \
		std::cout << "function: " << __FUNCTION__  << ", line: " <<  __LINE__ << std::endl; \
		system("pause"); \
	}\
}\


struct PersonInfo
{
	int age;
	std::string name;
	float height;

	// must implement
	friend Serializer& operator >> (Serializer& in, PersonInfo& d) {
		in >> d.age >> d.name >> d.height;
		return in;
	}
	friend Serializer& operator << (Serializer& out, PersonInfo d) {
		out << d.age << d.name << d.height;
		return out;
	}
};

int main()
{
	buttonrpc client;
	client.as_client("127.0.0.1", 5555);
	client.set_timeout(2000);

	int callcnt = 0;
	while (1){
		std::cout << "current call count: " << ++callcnt << std::endl;

		client.call<void>("foo_1");

		client.call<void>("foo_2", 10);

		int foo3r = client.call<int>("foo_3", 10).val();
		buttont_assert(foo3r == 100);

		int foo4r = client.call<int>("foo_4", 10, "buttonrpc", 100, (float)10.8).val();
		buttont_assert(foo4r == 1000);

		PersonInfo  dd = { 10, "buttonrpc", 170 };
		dd = client.call<PersonInfo>("foo_5", dd, 120).val();
		buttont_assert(dd.age == 20);
		buttont_assert(dd.name == "buttonrpc is good");
		buttont_assert(dd.height == 180);

		int foo6r = client.call<int>("foo_6", 10, "buttonrpc", 100).val();
		buttont_assert(foo6r == 1000);

		buttonrpc::value_t<void> xx = client.call<void>("foo_7", 666);
		buttont_assert(!xx.valid());

		sleep(1);
	}

	return 0;
}
