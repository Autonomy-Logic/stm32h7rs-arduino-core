// SPDX-License-Identifier: Apache-2.0

#include <cassert>
#include <cstdio>
#include <memory>

#include "ClientTable.h"

struct FakeSocket {
	bool open = true;
	int pending = 0;
};

/** Client stand-in with the members ClientTable uses; copies share one socket, like ZephyrClient. */
struct FakeClient {
	std::shared_ptr<FakeSocket> sock;
	FakeClient() = default;
	explicit FakeClient(std::shared_ptr<FakeSocket> s) : sock(std::move(s)) {}
	explicit operator bool() const { return sock != nullptr; }
	bool connected() const { return sock && sock->open; }
	int available() const { return connected() ? sock->pending : 0; }
	void stop() { if (sock) { sock->open = false; } }
};

static std::shared_ptr<FakeSocket> connect_one(ClientTable<FakeClient, 3> &table)
{
	auto s = std::make_shared<FakeSocket>();
	table.admit(FakeClient(s));
	return s;
}

static void three_clients_are_kept_and_a_fourth_is_closed()
{
	ClientTable<FakeClient, 3> table;
	auto a = connect_one(table);
	auto b = connect_one(table);
	auto c = connect_one(table);
	auto d = connect_one(table);
	assert(a->open && b->open && c->open);
	assert(!d->open);
	assert(table.count() == 3);
}

static void a_freed_slot_is_reused()
{
	ClientTable<FakeClient, 3> table;
	auto a = connect_one(table);
	connect_one(table);
	connect_one(table);
	a->open = false;
	auto d = connect_one(table);
	assert(d->open);
	assert(table.count() == 3);
}

static void pick_returns_only_connected_clients_with_data()
{
	ClientTable<FakeClient, 3> table;
	auto a = connect_one(table);
	auto b = connect_one(table);
	assert(!table.pick());
	b->pending = 4;
	FakeClient got = table.pick();
	assert(got && got.sock == b);
	b->open = false;
	a->pending = 1;
	got = table.pick();
	assert(got && got.sock == a);
}

static void pick_rotates_between_busy_clients()
{
	ClientTable<FakeClient, 3> table;
	auto a = connect_one(table);
	auto b = connect_one(table);
	a->pending = 1;
	b->pending = 1;
	FakeClient first = table.pick();
	FakeClient second = table.pick();
	assert(first.sock != second.sock);
}

static void an_invalid_client_is_ignored()
{
	ClientTable<FakeClient, 3> table;
	table.admit(FakeClient());
	assert(table.count() == 0);
}

int main()
{
	three_clients_are_kept_and_a_fourth_is_closed();
	a_freed_slot_is_reused();
	pick_returns_only_connected_clients_with_data();
	pick_rotates_between_busy_clients();
	an_invalid_client_is_ignored();
	std::puts("client table: ok");
	return 0;
}
