/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright 2023 Jérémie Galarneau <jeremie.galarneau@gmail.com>
 */

#ifndef NSEC_NETWORK_HANDLER_HPP
#define NSEC_NETWORK_HANDLER_HPP

#include "callback.hpp"
#include "config.hpp"
#include "network_messages.hpp"
#include "scheduler.hpp"

#include <SoftwareSerial.h>

namespace nsec::communication {

enum class peer_relative_position {
	LEFT,
	RIGHT,
};

using peer_id_t = uint8_t;

class network_handler : public scheduling::periodic_task {
public:
	using disconnection_notifier = void (*)();
	using pairing_begin_notifier = void (*)();
	// void (our_peer_id, peer count)
	using pairing_end_notifier = void (*)(peer_id_t, uint8_t);

	enum class application_message_action { SWALLOW, FORWARD, RESET };
	// application_message_action (relative_position_of_peer, message_type, message_payload)
	using message_received_notifier =
		application_message_action (*)(nsec::communication::message::type, const uint8_t *);

	network_handler(disconnection_notifier,
			pairing_begin_notifier,
			pairing_end_notifier,
			message_received_notifier) noexcept;

	/* Deactivate copy and assignment. */
	network_handler(const network_handler&) = delete;
	network_handler(network_handler&&) = delete;
	network_handler& operator=(const network_handler&) = delete;
	network_handler& operator=(network_handler&&) = delete;
	~network_handler() override = default;

	void setup() noexcept;

	enum class enqueue_message_result { QUEUED, UNCONNECTED, FULL };
	enqueue_message_result enqueue_message(peer_relative_position direction,
					       uint8_t msg_type,
					       const uint8_t *msg_payload,
					       uint8_t payload_size);

protected:
	void run(scheduling::absolute_time_ms current_time_ms) noexcept override;

private:
	enum class link_position {
		UNKNOWN = 0b00,
		LEFT_MOST = 0b01,
		RIGHT_MOST = 0b10,
		MIDDLE = 0b11
	};
	enum class wire_protocol_state {
		UNCONNECTED = 0,
		/* Wait for boards to listen before left-most initiates the discovery. */
		WAIT_TO_SEND_ANNOUNCE = 1,
		/* Discover neighbours, establish peer count, and peer id. */
		DISCOVERY = 2,
		/* Application controlled with automatic monitoring. */
		RUNNING = 3,
	};
	enum class message_reception_state {
		RECEIVE_MAGIC_BYTE_1,
		RECEIVE_MAGIC_BYTE_2,
		RECEIVE_HEADER,
		RECEIVE_PAYLOAD,
	};

	link_position _position() const noexcept;
	void _position(link_position new_role) noexcept;

	wire_protocol_state _wire_protocol_state() const noexcept;
	void _wire_protocol_state(wire_protocol_state state) noexcept;

	peer_relative_position _wave_front_direction() const noexcept;
	void _wave_front_direction(peer_relative_position) noexcept;
	void _reverse_wave_front_direction() noexcept;

	peer_relative_position _listening_side() const noexcept;
	SoftwareSerial& _listening_side_serial() noexcept;
	void _listening_side(peer_relative_position side) noexcept;
	void _reverse_listening_side() noexcept;

	message_reception_state _message_reception_state() const noexcept;
	void _message_reception_state(message_reception_state new_state) noexcept;

	peer_relative_position _pending_outgoing_message_direction() const noexcept;
	uint8_t _pending_outgoing_message_size() const noexcept;
	bool _has_pending_outgoing_message() const noexcept;
	void _clear_pending_outgoing_message() noexcept;

	enum class check_connections_result {
		NO_CHANGE,
		TOPOLOGY_CHANGED,
	};
	check_connections_result _check_connections() noexcept;

	void _detect_and_set_position() noexcept;
	void _run_wire_protocol(nsec::scheduling::absolute_time_ms current_time_ms) noexcept;
	void _handle_monitor_message(scheduling::absolute_time_ms current_time_ms) noexcept;
	void _reset() noexcept;

	bool _sense_is_left_connected() const noexcept;
	bool _sense_is_right_connected() const noexcept;

	enum class handle_message_result {
		SWALLOW = int(application_message_action::SWALLOW),
		FORWARD = int(application_message_action::FORWARD),
		RESET,
		SEND_ANNOUNCE,
		SEND_ANNOUNCE_REPLY,
		// Neighbor handed the talking stick to us.
		END_OF_PEER_TURN,
	};

	void _wire_protocol_discovery_handle_message(
		uint8_t type,
		const uint8_t *msg,
		scheduling::absolute_time_ms current_time_ms) noexcept;
	void _wire_protocol_running_handle_message(
		uint8_t type,
		const uint8_t *msg,
		scheduling::absolute_time_ms current_time_ms) noexcept;

	enum class handle_incoming_data_result {
		INCOMPLETE,
		COMPLETE,
	};
	handle_incoming_data_result _handle_incoming_data(SoftwareSerial&) noexcept;

	static void _log_wire_protocol_state(wire_protocol_state state) noexcept;
	static void _log_message_reception_state(message_reception_state state) noexcept;

	// Event handlers
	disconnection_notifier _notify_unconnected;
	pairing_begin_notifier _notify_pairing_begin;
	pairing_end_notifier _notify_pairing_end;
	message_received_notifier _notify_message_received;

	SoftwareSerial _left_serial;
	SoftwareSerial _right_serial;
	nsec::scheduling::absolute_time_ms _last_monitor_message_received_time_ms;

	uint8_t _is_left_connected : 1;
	uint8_t _is_right_connected : 1;

	// Storage for a link_position enum
	uint8_t _current_position : 2;
	// Storage for a wire_protocol_state enum
	uint8_t _current_wire_protocol_state : 2;
	// Storage for a peer_relative_location enum. Indicates the direction of the
	// wave front by the time we get the next message.
	uint8_t _current_wave_front_direction : 1;
	// Number of ticks in the current wire protocol state (only used by the wait state).
	uint8_t _ticks_in_wire_state : 2;
	// Storage for a peer_relative_location enum
	uint8_t _current_listening_side : 1;

	// This node's unique id in the network.
	peer_id_t _peer_id : 5;
	// Number of peers in the network (including this node).
	uint8_t _peer_count : 5;

	// Storage for a message_reception_state enum
	uint8_t _current_message_reception_state : 2;
	// Number of bytes left to receive for the current message
	uint8_t _payload_bytes_to_receive : 5;

	// Storage for a peer_relative_location enum
	uint8_t _current_pending_outgoing_message_direction : 1;
	uint8_t _current_pending_outgoing_message_size : 5;
	// Buffered outgoing message type and payload.
	uint8_t _current_pending_outgoing_message_type : 5;
	uint8_t _current_pending_outgoing_message_payload
		[nsec::config::communication::protocol_max_message_size];
};
} // namespace nsec::communication

#endif // NSEC_NETWORK_HANDLER_HPP
