// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ping.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/icmp6.h>
#include <netinet/ip_icmp.h>
#include <poll.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>

#define USEC_TO_MSEC(x) (float(x) / 1000.0)

const int MAX_PAYLOAD_SIZE_BYTES = 1400;

typedef struct {
  icmphdr hdr;
  uint8_t payload[MAX_PAYLOAD_SIZE_BYTES];
} __PACKED packet_t;

struct Options {
  unsigned int interval_usec = 1e6;
  long count = 3;
  int timeout_msec = 1000;
  uint32_t scope_id = 0;
  const char* host = nullptr;
  size_t payload_size_bytes = 0;
  size_t min_payload_size_bytes = 0;

  explicit Options(size_t min) {
    payload_size_bytes = min;
    min_payload_size_bytes = min;
  }

  void Print() const {
    printf("Count: %ld, ", count);
    printf("Payload size: %ld bytes, ", payload_size_bytes);
    printf("Interval: %u ms, ", interval_usec / 1000);
    printf("Timeout: %d ms, ", timeout_msec);
    printf("Scope ID: %u, ", scope_id);
    if (host != nullptr) {
      printf("Destination: %s\n", host);
    }
  }

  [[nodiscard]] bool Validate() const {
    if (interval_usec == 0) {
      fprintf(stderr, "interval must be greater than zero\n");
      return false;
    }

    if (payload_size_bytes >= MAX_PAYLOAD_SIZE_BYTES) {
      fprintf(stderr, "payload size must be smaller than: %d\n", MAX_PAYLOAD_SIZE_BYTES);
      return false;
    }

    if (payload_size_bytes < min_payload_size_bytes) {
      fprintf(stderr, "payload size must be more than: %ld\n", min_payload_size_bytes);
      return false;
    }

    if (count <= 0) {
      fprintf(stderr, "count must be positive: %ld\n", count);
      return false;
    }

    if (timeout_msec <= 0) {
      fprintf(stderr, "timeout must be positive: %d\n", timeout_msec);
      return false;
    }

    if (host == nullptr) {
      fprintf(stderr, "destination must be provided\n");
      return false;
    }
    return true;
  }

  [[nodiscard]] int Usage() const {
    fprintf(stderr, "\n\tUsage: ping [ <option>* ] destination\n");
    fprintf(stderr, "\n\tSend ICMP ECHO_REQUEST to a destination. This destination\n");
    fprintf(stderr, "\tmay be a hostname (google.com) or an IP address (8.8.8.8).\n\n");
    fprintf(stderr, "\t-c count: Only send count packets (default = 3)\n");
    fprintf(stderr, "\t-i interval(ms): Time interval between pings (default = 1000)\n");
    fprintf(stderr, "\t-t timeout(ms): Timeout waiting for ping response (default = 1000)\n");
    fprintf(stderr, "\t-I scope_id: IPv6 address scope ID (default = 0)\n");
    fprintf(stderr, "\t-s size(bytes): Number of payload bytes (default = %ld, max 1400)\n",
            payload_size_bytes);
    fprintf(stderr, "\t-h: View this help message\n\n");
    return -1;
  }

  int ParseCommandLine(int argc, char** argv) {
    int opt;
    while ((opt = getopt(argc, argv, "s:c:i:t:I:h")) != -1) {
      char* endptr = nullptr;
      switch (opt) {
        case 'h':
          return Usage();
        case 'i':
          interval_usec = static_cast<unsigned int>(strtoul(optarg, &endptr, 10) * 1000);
          if (*endptr != '\0') {
            fprintf(stderr, "-i must be followed by a non-negative integer\n");
            return Usage();
          }
          break;
        case 's':
          payload_size_bytes = strtol(optarg, &endptr, 10);
          if (*endptr != '\0') {
            fprintf(stderr, "-s must be followed by a non-negative integer\n");
            return Usage();
          }
          break;
        case 'c':
          count = strtol(optarg, &endptr, 10);
          if (*endptr != '\0') {
            fprintf(stderr, "-c must be followed by a non-negative integer\n");
            return Usage();
          }
          break;
        case 't':
          timeout_msec = static_cast<int>(strtol(optarg, &endptr, 10));
          if (*endptr != '\0') {
            fprintf(stderr, "-t must be followed by a non-negative integer\n");
            return Usage();
          }
          break;
        case 'I':
          scope_id = static_cast<uint32_t>(strtoul(optarg, &endptr, 10));
          if (*endptr != '\0') {
            fprintf(stderr, "-I must be followed by a non-negative integer\n");
            return Usage();
          }
          break;
        default:
          return Usage();
      }
    }
    if (optind >= argc) {
      fprintf(stderr, "missing destination\n");
      return Usage();
    }
    host = argv[optind];
    return 0;
  }
};

struct PingStatistics {
  uint64_t min_rtt_usec = UINT64_MAX;
  uint64_t max_rtt_usec = 0;
  uint64_t sum_rtt_usec = 0;
  uint16_t num_sent = 0;
  uint16_t num_lost = 0;

  void Update(uint64_t rtt_usec) {
    if (rtt_usec < min_rtt_usec) {
      min_rtt_usec = rtt_usec;
    }
    if (rtt_usec > max_rtt_usec) {
      max_rtt_usec = rtt_usec;
    }
    sum_rtt_usec += rtt_usec;
    num_sent++;
  }

  void Print() const {
    if (num_sent == 0) {
      printf("No echo request sent\n");
      return;
    }
    printf("RTT Min/Max/Avg = [ %.3f / %.3f / %.3f ] ms\n", USEC_TO_MSEC(min_rtt_usec),
           USEC_TO_MSEC(max_rtt_usec), USEC_TO_MSEC(sum_rtt_usec / num_sent));
  }
};

bool ValidateReceivedPacket(const packet_t& sent_packet, size_t sent_packet_size,
                            const packet_t& received_packet, size_t received_packet_size,
                            const Options& options) {
  if (received_packet_size != sent_packet_size) {
    fprintf(stderr, "Incorrect Packet size of received packet: %zu expected %zu\n",
            received_packet_size, sent_packet_size);
    return false;
  }
  uint8_t expected_type;
  switch (sent_packet.hdr.type) {
    case ICMP_ECHO:
      expected_type = ICMP_ECHOREPLY;
      break;
    case ICMP6_ECHO_REQUEST:
      expected_type = ICMP6_ECHO_REPLY;
      break;
    default:
      fprintf(stderr, "Incorrect Header type in sent packet: %d\n", sent_packet.hdr.type);
      return false;
  }
  if (received_packet.hdr.type != expected_type) {
    fprintf(stderr, "Incorrect Header type in received packet: %d expected: %d\n",
            received_packet.hdr.type, expected_type);
    return false;
  }
  if (received_packet.hdr.code != 0) {
    fprintf(stderr, "Incorrect Header code in received packet: %d expected: 0\n",
            received_packet.hdr.code);
    return false;
  }

  // Do not match identifier.
  // RFC 792 and RFC 1122 3.2.2.6 require the echo reply carries the same value
  // from the echo request, implementations honor that. But the requester side
  // NAT may rewrite the identifier on echo request, which makes impossible
  // for the host to match.

  if (received_packet.hdr.un.echo.sequence != sent_packet.hdr.un.echo.sequence) {
    fprintf(stderr, "Incorrect Header sequence in received packet: %d expected: %d\n",
            received_packet.hdr.un.echo.sequence, sent_packet.hdr.un.echo.sequence);
    return false;
  }
  if (memcmp(received_packet.payload, sent_packet.payload, options.payload_size_bytes) != 0) {
    fprintf(stderr, "Incorrect Payload content in received packet\n");
    return false;
  }
  return true;
}

int ping(int argc, char* argv[]) {
  constexpr char ping_message[] = "This is an echo message!";
  Options options(strlen(ping_message) + 1);
  PingStatistics stats;

  if (options.ParseCommandLine(argc, argv) != 0) {
    return -1;
  }

  if (!options.Validate()) {
    return options.Usage();
  }

  options.Print();

  struct addrinfo hints = {};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_RAW;
  hints.ai_flags = 0;
  struct addrinfo* info;
  if (getaddrinfo(options.host, nullptr, &hints, &info)) {
    fprintf(stderr, "ping: unknown host %s\n", options.host);
    return -1;
  }

  int proto;
  uint8_t type;
  switch (info->ai_family) {
    case AF_INET: {
      proto = IPPROTO_ICMP;
      type = ICMP_ECHO;
      char buf[INET_ADDRSTRLEN];
      auto addr = reinterpret_cast<struct sockaddr_in*>(info->ai_addr);
      printf("PING4 %s (%s)\n", options.host,
             inet_ntop(info->ai_family, &addr->sin_addr, buf, sizeof(buf)));
      break;
    }
    case AF_INET6: {
      proto = IPPROTO_ICMPV6;
      type = ICMP6_ECHO_REQUEST;
      char buf[INET6_ADDRSTRLEN];
      auto addr = reinterpret_cast<struct sockaddr_in6*>(info->ai_addr);
      addr->sin6_scope_id = options.scope_id;
      printf("PING6 %s (%s)\n", options.host,
             inet_ntop(info->ai_family, &addr->sin6_addr, buf, sizeof(buf)));
      break;
    }
    default:
      fprintf(stderr, "ping: unknown address family %d\n", info->ai_family);
      return -1;
  }

  int s = socket(info->ai_family, SOCK_DGRAM, proto);
  if (s < 0) {
    fprintf(stderr, "Could not acquire ICMP socket: %s\n", strerror(errno));
    return -1;
  }

  const zx_ticks_t ticks_per_usec = zx_ticks_per_second() / 1000000;

  packet_t packet = {};
  packet.hdr.type = type;
  packet.hdr.code = 0;
  packet.hdr.un.echo.id = 0;
  strcpy(reinterpret_cast<char*>(packet.payload), ping_message);

  uint16_t sequence = 1;
  while (options.count-- > 0) {
    packet.hdr.un.echo.sequence = htons(sequence++);
    // Netstack will overwrite the checksum
    zx_ticks_t before = zx_ticks_get();
    ssize_t sent_packet_size = sizeof(packet.hdr) + options.payload_size_bytes;
    ssize_t r = sendto(s, &packet, sent_packet_size, 0, info->ai_addr, info->ai_addrlen);
    if (r < 0) {
      fprintf(stderr, "ping: Could not send packet\n");
      return -1;
    }

    struct pollfd fd = {};
    fd.fd = s;
    fd.events = POLLIN;
    switch (poll(&fd, 1, options.timeout_msec)) {
      case 0:
        fprintf(stderr, "ping: Timeout after %d ms\n", options.timeout_msec);
        return -1;
      case 1:
        if (fd.revents & POLLIN) {
          packet_t received_packet;
          r = recvfrom(s, &received_packet, sizeof(received_packet), 0, nullptr, nullptr);
          if (r < 0) {
            fprintf(stderr, "ping: Could not read result of ping\n");
            return -1;
          }
          if (!ValidateReceivedPacket(packet, sent_packet_size, received_packet, r, options)) {
            fprintf(stderr, "ping: Received packet didn't match sent packet: %d\n", sequence);
          } else {
            zx_ticks_t after = zx_ticks_get();
            uint64_t usec = (after - before) / ticks_per_usec;
            stats.Update(usec);
            printf("%zu bytes from %s : icmp_seq=%d rtt=%.3f ms\n", r, options.host, sequence,
                   (float)usec / 1000.0);
          }
          if (options.count > 0) {
            usleep(options.interval_usec);
          }
          continue;
        }
        __FALLTHROUGH;
      default:
        fprintf(stderr, "ping: Spurious wakeup from poll\n");
        return -1;
    }
  }
  freeaddrinfo(info);
  stats.Print();
  close(s);
  return 0;
}
