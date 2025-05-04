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
#define SEQSPACE    (2*WINDOWSIZE)  /* Sequence number space must be at least 2*WindowSize */
#define NOTINUSE (-1)   /* used to fill header fields that are not being used */

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

static struct pkt sr_buffer[SEQSPACE];  /* array for storing packets waiting for ACK */
static bool sr_acked[SEQSPACE];         /* array for storing which packets have been ACKed */
static int sr_timerids[SEQSPACE];       /* array for tracking if timer is running for this packet */
static int base;                        /* base sequence number (start of window) */
static int nextseqnum;                  /* next sequence number to use */

/* called from layer 5 (application layer), passed the message to be sent to other side */
void A_output(struct msg message)
{
    struct pkt sendpkt;
    int i;
    
    /* check if window is not full */
    if ((nextseqnum < base + WINDOWSIZE) && ((nextseqnum - base) < WINDOWSIZE)) {
        if (TRACE > 1)
            printf("----A: New message arrives, send window is not full, send new message to layer3!\n");
        
        /* create packet */
        sendpkt.seqnum = nextseqnum % SEQSPACE;
        sendpkt.acknum = NOTINUSE;
        for (i = 0; i < 20; i++)
            sendpkt.payload[i] = message.data[i];
        sendpkt.checksum = ComputeChecksum(sendpkt);
        
        /* store packet in buffer */
        sr_buffer[sendpkt.seqnum] = sendpkt;
        sr_acked[sendpkt.seqnum] = false;
        sr_timerids[sendpkt.seqnum] = 1;  /* mark that timer should be started for this packet */
        
        /* send packet */
        if (TRACE > 0)
            printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
        tolayer3(A, sendpkt);
        
        /* start timer for this packet */
        starttimer(A, RTT);
        
        /* increment sequence number */
        nextseqnum = (nextseqnum + 1) % SEQSPACE;
    }
    else {
        if (TRACE > 0)
            printf("----A: New message arrives, send window is full\n");
        window_full++;
    }
}

/* called from layer 3, when a packet arrives for layer 4 */
void A_input(struct pkt packet)
{
    int acknum;
    
    /* check if packet is corrupted */
    if (!IsCorrupted(packet)) {
        if (TRACE > 0)
            printf("----A: uncorrupted ACK %d is received\n", packet.acknum);
        
        total_ACKs_received++;
        acknum = packet.acknum;
        
        /* check if ACK is within current window */
        if (((base <= acknum) && (acknum < nextseqnum)) || 
            ((base <= acknum) && (nextseqnum < base)) ||
            ((acknum < nextseqnum) && (nextseqnum < base))) {
            
            /* mark packet as ACKed */
            if (!sr_acked[acknum]) {
                sr_acked[acknum] = true;
                new_ACKs++;
                
                if (TRACE > 0)
                    printf("----A: ACK %d is not a duplicate\n", acknum);
                
                /* stop timer for this packet */
                stoptimer(A);
                sr_timerids[acknum] = 0;
                
                /* if base packet is ACKed, slide window */
                while (sr_acked[base] && (base != nextseqnum)) {
                    base = (base + 1) % SEQSPACE;
                }
                
                /* restart timer for oldest unACKed packet if needed */
                if (base != nextseqnum) {
                    int i = base;
                    while (i != nextseqnum) {
                        if (!sr_acked[i]) {
                            starttimer(A, RTT);
                            break;
                        }
                        i = (i + 1) % SEQSPACE;
                    }
                }
            }
            else {
                if (TRACE > 0)
                    printf("----A: duplicate ACK %d received, do nothing\n", acknum);
            }
        }
    }
    else {
        if (TRACE > 0)
            printf("----A: corrupted ACK is received, do nothing!\n");
    }
}

/* called when A's timer goes off */
void A_timerinterrupt(void)
{
    int i;
    
    if (TRACE > 0)
        printf("----A: timeout, resend unACKed packets!\n");
    
    /* find the oldest unACKed packet */
    for (i = base; i != nextseqnum; i = (i + 1) % SEQSPACE) {
        if (!sr_acked[i]) {
            if (TRACE > 0)
                printf("---A: resending packet %d\n", i);
            
            /* resend packet */
            tolayer3(A, sr_buffer[i]);
            packets_resent++;
            
            /* restart timer */
            starttimer(A, RTT);
            break;
        }
    }
}

/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void A_init(void) {
    int i;
    
    /* initialize sender variables */
    base = 0;
    nextseqnum = 0;
    
    /* initialize arrays */
    for (i = 0; i < SEQSPACE; i++) {
        sr_acked[i] = false;
        sr_timerids[i] = 0;
    }
}



/********* Receiver (B) variables and procedures ************/

static struct pkt rb_buffer[SEQSPACE];  /* buffer for out-of-order packets */
static bool rb_received[SEQSPACE];      /* array for tracking which packets have been received */
static int rb_base;                     /* base sequence number (start of window) */

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void)
{
    int i;
    
    /* initialize receiver variables */
    rb_base = 0;
    
    /* initialize arrays */
    for (i = 0; i < SEQSPACE; i++) {
        rb_received[i] = false;
    }
}

/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet)
{
    struct pkt ack_pkt;
    int i;
    int seqnum;
    
    /* check if packet is corrupted */
    if (!IsCorrupted(packet)) {
        seqnum = packet.seqnum;
        
        /* check if packet is within current window */
        if (((rb_base <= seqnum) && (seqnum < rb_base + WINDOWSIZE)) || 
            ((rb_base <= seqnum + SEQSPACE) && (seqnum + SEQSPACE < rb_base + WINDOWSIZE))) {
            
            /* buffer packet if not already received */
            if (!rb_received[seqnum]) {
                if (TRACE > 0)
                    printf("----B: packet %d is correctly received, buffer it\n", seqnum);
                
                rb_buffer[seqnum] = packet;
                rb_received[seqnum] = true;
                
                /* deliver in-order packets to layer 5 */
                while (rb_received[rb_base]) {
                    if (TRACE > 0)
                        printf("----B: delivering packet %d to layer 5\n", rb_base);
                    
                    tolayer5(B, rb_buffer[rb_base].payload);
                    packets_received++;
                    
                    /* mark as no longer received (in case of wraparound) */
                    rb_received[rb_base] = false;
                    
                    /* move window forward */
                    rb_base = (rb_base + 1) % SEQSPACE;
                }
            }
            
            /* send ACK */
            ack_pkt.seqnum = NOTINUSE;
            ack_pkt.acknum = seqnum;
            for (i = 0; i < 20; i++)
                ack_pkt.payload[i] = '0';
            ack_pkt.checksum = ComputeChecksum(ack_pkt);
            
            if (TRACE > 0)
                printf("----B: sending ACK %d\n", seqnum);
            
            tolayer3(B, ack_pkt);
        }
        else if (((rb_base - WINDOWSIZE <= seqnum) && (seqnum < rb_base)) ||
                 ((rb_base - WINDOWSIZE <= seqnum + SEQSPACE) && (seqnum + SEQSPACE < rb_base))) {
            /* This is a packet from the previous window, still ACK it */
            if (TRACE > 0)
                printf("----B: packet %d is from previous window, still ACK it\n", seqnum);
            
            ack_pkt.seqnum = NOTINUSE;
            ack_pkt.acknum = seqnum;
            for (i = 0; i < 20; i++)
                ack_pkt.payload[i] = '0';
            ack_pkt.checksum = ComputeChecksum(ack_pkt);
            
            tolayer3(B, ack_pkt);
        }
    }
    else {
        if (TRACE > 0)
            printf("----B: corrupted packet received, do nothing\n");
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

