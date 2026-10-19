# Latency stage of SpeedTest: measures the latency to the server with Net::Ping.
# The Rust client runs it with `perl -e` and reads its standard output.
#
# Arguments: <host> <protocol: icmp|tcp|udp> <count> <server port>
# Output, one line per ping:
#   ping <n> <ip> <latency> ms
#   ping <n> <host> timeout
use strict;
use warnings;
use Net::Ping;

my ($host, $protocol, $count, $port) = @ARGV;
die "uso: perl ping.pl <host> <icmp|tcp|udp> <cantidad> <puerto>\n" unless defined $host;
$protocol ||= 'icmp';
$count    ||= 4;

my $pinger = Net::Ping->new($protocol, 2);    # 2 second timeout per ping
$pinger->hires(1);                            # times with microsecond resolution (Time::HiRes)
$pinger->port_number($port) if $protocol eq 'tcp' && $port;    # TCP ping = connect() to the server port

for my $n (1 .. $count) {
    my ($ok, $duration, $ip) = $pinger->ping($host);
    if ($ok) {
        printf "ping %d %s %.3f ms\n", $n, $ip // $host, $duration * 1000;
    } else {
        printf "ping %d %s timeout\n", $n, $host;
    }
}
$pinger->close();
