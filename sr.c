#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "emulator.h"
#include "sr.h"

/* ******************************************************************
   Go Back N protocol.  Adapted from J.F.Kurose
   ALTERNATING BIT AND GO-BACK-N NETWORK EMULATOR: VERSION 1.2

   Network properties:
   - one way network delay averages five time units (longer if there
   are other messages in the channel for GBN), but can be larger
   - packets can be corrupted (either the header or the data portion)
   or lost, according to user-defined probabilities
   - packets will be delivered in the order in which they were sent
   (although some can be lost).

   Modifications:
   - removed bidirectional GBN code and other code not used by prac.
   - fixed C style to adhere to current programming style
   - added GBN implementation
**********************************************************************/

#define RTT  16.0       /* round trip time.  MUST BE SET TO 16.0 when submitting assignment */
#define WINDOWSIZE 6    /* the maximum number of buffered unacked packet
                          MUST BE SET TO 6 when submitting assignment */
#define SEQSPACE    (2*WINDOWSIZE)

static struct pkt sr_buffer[SEQSPACE]; 
static bool    sr_acked[SEQSPACE];
static double  sr_expiry[SEQSPACE];  
static int     base, nextseqnum;

/* generic procedure to compute the checksum of a packet.  Used by both sender and receiver
   the simulator will overwrite part of your packet with 'z's.  It will not overwrite your
   original checksum.  This procedure must generate a different checksum to the original if
   the packet is corrupted.
*/
int ComputeChecksum(struct pkt packet)
{
  int checksum = 0;
  int i;

  checksum = packet.seqnum;
  checksum += packet.acknum;
  for ( i=0; i<20; i++ )
    checksum += (int)(packet.payload[i]);

  return checksum;
}

bool IsCorrupted(struct pkt packet)
{
  if (packet.checksum == ComputeChecksum(packet))
    return (false);
  else
    return (true);
}


/********* Sender (A) variables and functions ************/
/* called from layer 5 (application layer), passed the message to be sent to other side */
void A_output(struct msg message) {
    // If the send window is not full
    if (nextseqnum < base + WINDOWSIZE) {
        struct pkt p;
        p.seqnum  = nextseqnum;
        p.acknum  = -1;
        memcpy(p.payload, message.data, 20);
        p.checksum = ComputeChecksum(p);

        // Store the packet in the buffer and mark it as not yet ACKed
        sr_buffer[nextseqnum] = p;
        sr_acked[nextseqnum]  = false;

        // Send the packet into the network layer
        tolayer3(A, p);

        // If this is the very first packet in the window, start the timer
        if (base == nextseqnum) {
            starttimer(A, RTT);
        }

        // Record this packet’s timeout expiration (simulated)
        sr_expiry[nextseqnum] = RTT;
        nextseqnum++;
    }
    // Otherwise: send window is full—drop or defer the message (same as GBN behavior)
}


/* called from layer 3, when a packet arrives for layer 4
   In this practical this will always be an ACK as B never sends data.
*/
void A_input(struct pkt packet) {
    // Process only if the checksum is valid
    if (packet.checksum == ComputeChecksum(packet)) {
        int seq = packet.acknum;

        // If the ACK is for a packet in the current window, mark it as received
        if (seq >= base && seq < base + WINDOWSIZE) {
            sr_acked[seq] = true;
        }

        // Slide the window forward past all consecutively ACKed packets
        while (base < nextseqnum && sr_acked[base]) {
            base++;
        }

        // Stop or restart the timer based on whether there are outstanding packets
        if (base == nextseqnum) {
            // No unACKed packets remain, so stop the timer
            stoptimer(A);
        } else {
            // There are still unACKed packets; restart the timer for the new base
            stoptimer(A);
            starttimer(A, RTT);
        }
    }
}


/* called when A's timer goes off */
void A_timerinterrupt(void) {
    // Iterate over all packets in the window [base, nextseqnum)
    for (int i = base; i < nextseqnum; i++) {
        // If a packet is unACKed and its timeout has expired
        if (!sr_acked[i] && sr_expiry[i] <= RTT) {
            // Retransmit the timed-out packet
            tolayer3(A, sr_buffer[i]);
            packets_resent++;
        }
    }
    // Restart the timer for the next round-trip interval
    starttimer(A, RTT);
}




/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void A_init(void) {
    base       = 0;
    nextseqnum = 0;
    for (int i = 0; i < SEQSPACE; i++) {
        sr_acked[i] = false;
        sr_expiry[i] = 0.0;
    }
}



/********* Receiver (B)  variables and procedures ************/

static struct pkt rb_buffer[SEQSPACE];  // Buffer to hold out-of-order packets
static bool    rb_received[SEQSPACE];   // Flags indicating which sequence numbers have been received
static int     expectedseq;             // Next in-order sequence number expected

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void) {
    // Initialize expected sequence number and clear receive flags
    expectedseq = 0;
    for (int i = 0; i < SEQSPACE; i++) {
        rb_received[i] = false;
    }
}

/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet) {
    // Discard any corrupted packet
    if (packet.checksum != ComputeChecksum(packet)) {
        return;
    }

    int seq = packet.seqnum;
    // If packet is within the receive window, buffer it
    if (seq >= expectedseq && seq < expectedseq + WINDOWSIZE) {
        rb_buffer[seq]   = packet;
        rb_received[seq] = true;
    }

    // Deliver all consecutively received packets to the application layer
    while (rb_received[expectedseq]) {
        tolayer5(B, rb_buffer[expectedseq].payload);
        rb_received[expectedseq] = false;
        expectedseq++;
    }

    // Send an individual ACK for this packet back to sender
    struct pkt ackp;
    ackp.seqnum   = 0;
    ackp.acknum   = seq;
    ackp.checksum = ComputeChecksum(ackp);
    tolayer3(B, ackp);
}


/******************************************************************************
 * The following functions need be completed only for bi-directional messages *
 *****************************************************************************/

/* Note that with simplex transfer from a-to-B, there is no B_output() */
void B_output(struct msg message)
{
}

/* called when B's timer goes off */
void B_timerinterrupt(void)
{
}

