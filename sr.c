#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "emulator.h"
#include "sr.h"

/* ******************************************************************
   Selective Repeat protocol.
**********************************************************************/

/* C89 boolean type */
typedef enum { false = 0, true = 1 } bool;

/* Protocol parameters */
#define RTT  16.0       /* round trip time.  MUST BE SET TO 16.0 when submitting assignment */
#define WINDOWSIZE 6    /* the maximum number of buffered unacked packet
                          MUST BE SET TO 6 when submitting assignment */
#define SEQSPACE    (2*WINDOWSIZE)

/* Selective Repeat sender state */
static struct pkt sr_buffer[SEQSPACE]; 
static bool    sr_acked[SEQSPACE];
static double  sr_timer[SEQSPACE];  
static int     base, nextseqnum;

/* Compute checksum of a packet */
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
void A_output(struct msg message)
{
    struct pkt p;
    /* if send window not full */
    if (nextseqnum < base + WINDOWSIZE) {
        if (TRACE > 1)
        printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");

        p.seqnum = nextseqnum;
        p.acknum = -1;
        memcpy(p.payload, message.data, 20);
        p.checksum = ComputeChecksum(p);

        sr_buffer[nextseqnum] = p;
        sr_acked[nextseqnum] = false;

        if (TRACE > 0)
            printf("Sending packet %d to layer 3\n", p.seqnum);
        tolayer3(A, p);

        /* if first outstanding packet, start timer */
        if (base == nextseqnum)
            starttimer(A, RTT);
        /* record its expiry */
        sr_timer[nextseqnum] = RTT;
        nextseqnum++;
    } else {
        if (TRACE > 0)
            printf("----A: New message arrives, send window is full\n");
        window_full++;
    }
}


/* called from layer 3, when a packet arrives for layer 4
   In this practical this will always be an ACK as B never sends data.
*/
void A_input(struct pkt packet)
{
    int seq;
    /* only process valid ACKs */
    if (!IsCorrupted(packet)) {
        if (TRACE > 0)
            printf("----A: uncorrupted ACK %d is received\n", packet.acknum);

        total_ACKs_received++;
        seq = packet.acknum;

        if (TRACE > 0)
            printf("----A: ACK %d is not a duplicate\n", seq);
        new_ACKs++;

        /* if ACK in window mark as received */
        if (seq >= base && seq < base + WINDOWSIZE)
            sr_acked[seq] = true;

        /* slide window over all ACKed packets */
        while (base < nextseqnum && sr_acked[base])
            base++;

        stoptimer(A);
        if (base < nextseqnum)
            starttimer(A, RTT);
    } else {
        if (TRACE > 0)
        printf("----A: corrupted ACK is received, do nothing!\n");
    }
}


/* called when A's timer goes off */
void A_timerinterrupt(void)
{
    if (TRACE > 0)
        printf("----A: time out,resend packets!\n");

    if (!sr_acked[base]) {
        if (TRACE > 0)
            printf("---A: resending packet %d\n", sr_buffer[base].seqnum);
        tolayer3(A, sr_buffer[base]);
        packets_resent++;
    }

    if (base < nextseqnum)
        starttimer(A, RTT);
}


/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void A_init(void) {
    int i;
    base = 0;
    nextseqnum = 0;
    for (i = 0; i < SEQSPACE; i++) {
        sr_acked[i] = false;
        sr_timer[i] = 0.0;
    }
}



/********* Receiver (B)  variables and procedures ************/

static struct pkt rb_buffer[SEQSPACE];  /* buffer for out-of-order packets */
static bool        rb_received[SEQSPACE]; /* received flags */
static int         expectedseq;           /* next in-order seq expected */

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void)
{
    int i;
    expectedseq = 0;
    for (i = 0; i < SEQSPACE; i++)
        rb_received[i] = false;
}


/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet)
{
    int seq;
    struct pkt ackp;

    if (IsCorrupted(packet)) return;

    seq = packet.seqnum;

    if (TRACE > 0)
        printf("----B: packet %d is correctly received, send ACK!\n", seq);

    packets_received++;

    ackp.seqnum = 0;
    ackp.acknum = seq;
    ackp.checksum = ComputeChecksum(ackp);
    tolayer3(B, ackp);

    if (seq >= expectedseq && seq < expectedseq + WINDOWSIZE) {
        if (!rb_received[seq]) {
            rb_buffer[seq] = packet;
            rb_received[seq] = true;
        }
    }

    while (rb_received[expectedseq]) {
        tolayer5(B, rb_buffer[expectedseq].payload);
        rb_received[expectedseq] = false;
        expectedseq++;
    }
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

