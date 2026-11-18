/*
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>

/**
 * Fixed table of accepted server connections. A connection arriving while every slot holds a connected client is
 * accepted and closed at once; a slot whose client disconnected is reused.
 *
 * @tparam Client copyable client type with explicit operator bool, connected(), available() and stop().
 * @tparam N number of clients served at the same time.
 */
template <typename Client, size_t N> class ClientTable {
public:
	/** Stores a newly accepted client in a free slot, or closes it when the table is full. */
	void admit(Client client)
	{
		if (!client) {
			return;
		}
		for (size_t i = 0; i < N; i++) {
			if (!_slots[i] || !_slots[i].connected()) {
				_slots[i] = client;
				return;
			}
		}
		client.stop();
	}

	/** Returns a connected client with data to read, rotating between slots; an empty client when none has data. */
	Client pick()
	{
		for (size_t n = 0; n < N; n++) {
			size_t i = (_next + n) % N;
			if (_slots[i] && _slots[i].connected() && _slots[i].available() > 0) {
				_next = (i + 1) % N;
				return _slots[i];
			}
		}
		return Client();
	}

	/** Returns the number of slots holding a connected client. */
	size_t count()
	{
		size_t used = 0;
		for (size_t i = 0; i < N; i++) {
			if (_slots[i] && _slots[i].connected()) {
				used++;
			}
		}
		return used;
	}

private:
	Client _slots[N];
	size_t _next = 0;
};
