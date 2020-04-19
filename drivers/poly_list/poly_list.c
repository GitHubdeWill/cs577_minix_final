#include <minix/drivers.h>
#include <minix/chardriver.h>
#include <minix/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <minix/ds.h>
#include <stdlib.h>
#include "poly_list.h"

/*
 * Function prototypes for the poly_list driver.
 */
static int poly_list_open(message *m);
static int poly_list_close(message *m);
static struct device * poly_list_prepare(dev_t device);
static int poly_list_transfer(endpoint_t endpt, int opcode, u64_t position,
	iovec_t *iov, unsigned int nr_req, endpoint_t user_endpt, unsigned int
	flags);
static int poly_list_ioctl(message *m);

/* SEF functions and variables. */
static void sef_local_startup(void);
static int sef_cb_init(int type, sef_init_info_t *info);
static int sef_cb_lu_state_save(int);
static int lu_state_restore(void);
static int yesno;
static int msgCase = DEFAULTPOLY_LIST8;

/* Entry points to the poly_list driver. */
static struct chardriver poly_list_tab =
{
    poly_list_open,
    poly_list_close,
    poly_list_ioctl,
    poly_list_prepare,
    poly_list_transfer,
    nop_cleanup,
    nop_alarm,
    nop_cancel,
    nop_select,
    NULL
};

/** Represents the /dev/poly_list device. */
static struct device poly_list_device;

/** State variable to count the number of times the device has been opened. */
static int open_counter;

static int poly_list_open(message *UNUSED(m))
{
    yesno = random();
    printf("Random number generated: %d\n", yesno);
    printf("poly_list_open(). Called %d time(s).\n", ++open_counter);
    return OK;
}

static int poly_list_close(message *UNUSED(m))
{
    printf("poly_list_close()\n");
    return OK;
}

static struct device * poly_list_prepare(dev_t UNUSED(dev))
{
    poly_list_device.dv_base = make64(0, 0);
    poly_list_device.dv_size = make64(strlen(POLY_LIST_MESSAGE), 0);
    return &poly_list_device;
}

static int poly_list_ioctl(message *m_ptr){
    printf("poly_list_ioctl()\n");
    printf("Message source: %d\n", m_ptr->m_source);
    printf("Message type: %d\n", m_ptr->m_type);
    printf("Message one: %d\n", m_ptr->m_u.m_m1.m1i1);
    printf("Message: %d\n", m_ptr->COUNT);
    switch(m_ptr->COUNT) {
        case LOWERPOLY_LIST8:
            msgCase = LOWERPOLY_LIST8;
            break;
        case UPPERPOLY_LIST8:
            msgCase = UPPERPOLY_LIST8;
            break;
        default:
            msgCase = DEFAULTPOLY_LIST8;
            break;
    }
    return 0;
}

static int poly_list_transfer(endpoint_t endpt, int opcode, u64_t position,
    iovec_t *iov, unsigned nr_req, endpoint_t UNUSED(user_endpt),
    unsigned int UNUSED(flags))
{
    int bytes, ret;
    char poly_list_message[4];

    // Convert to 1/0 (true/false, yes/no).
    yesno%=2;
    switch(msgCase) {
        case LOWERPOLY_LIST8:
            memcpy(poly_list_message, (yesno) ? POLY_LIST_YES_LOWER : POLY_LIST_NO_LOWER, sizeof poly_list_message);
            break;
        case UPPERPOLY_LIST8:
            memcpy(poly_list_message, (yesno) ? POLY_LIST_YES_UPPER : POLY_LIST_NO_UPPER, sizeof poly_list_message);
            break;
        default:
            memcpy(poly_list_message, (yesno) ? POLY_LIST_YES : POLY_LIST_NO, sizeof poly_list_message);
            break;
    }
    msgCase=DEFAULTPOLY_LIST8;


    printf("poly_list_transfer()\n");
    printf("Results in %s\n", poly_list_message);


    if (nr_req != 1)
    {
        /* This should never trigger for character drivers at the moment. */
        printf("poly_list: vectored transfer request, using first element only\n");
    }

    bytes = strlen(poly_list_message) - ex64lo(position) < iov->iov_size ?
            strlen(poly_list_message) - ex64lo(position) : iov->iov_size;

    if (bytes <= 0)
    {
        return OK;
    }
    switch (opcode)
    {
        case DEV_GATHER_S:
            ret = sys_safecopyto(endpt, (cp_grant_id_t) iov->iov_addr, 0,
                                (vir_bytes) (poly_list_message + ex64lo(position)),
                                 bytes);
            iov->iov_size -= bytes;
            break;

        default:
            return EINVAL;
    }
    return ret;
}

static int sef_cb_lu_state_save(int UNUSED(state)) {
/* Save the state. */
    ds_publish_u32("open_counter", open_counter, DSF_OVERWRITE);

    return OK;
}

static int lu_state_restore() {
/* Restore the state. */
    u32_t value;

    ds_retrieve_u32("open_counter", &value);
    ds_delete_u32("open_counter");
    open_counter = (int) value;

    return OK;
}

static void sef_local_startup()
{
    /*
     * Register init callbacks. Use the same function for all event types
     */
    sef_setcb_init_fresh(sef_cb_init);
    sef_setcb_init_lu(sef_cb_init);
    sef_setcb_init_restart(sef_cb_init);

    /*
     * Register live update callbacks.
     */
    /* - Agree to update immediately when LU is requested in a valid state. */
    sef_setcb_lu_prepare(sef_cb_lu_prepare_always_ready);
    /* - Support live update starting from any standard state. */
    sef_setcb_lu_state_isvalid(sef_cb_lu_state_isvalid_standard);
    /* - Register a custom routine to save the state. */
    sef_setcb_lu_state_save(sef_cb_lu_state_save);

    /* Let SEF perform startup. */
    sef_startup();
}

static int sef_cb_init(int type, sef_init_info_t *UNUSED(info))
{
/* Initialize the poly_list driver. */
    int do_announce_driver = TRUE;

    open_counter = 0;
    switch(type) {
        case SEF_INIT_FRESH:
            printf("%s", "I've been started!");
        break;

        case SEF_INIT_LU:
            /* Restore the state. */
            lu_state_restore();
            do_announce_driver = FALSE;

            printf("Hey, I'm a new version!\n");
        break;

        case SEF_INIT_RESTART:
            printf("Hey, I've just been restarted!\n");
        break;
    }

    /* Announce we are up when necessary. */
    if (do_announce_driver) {
        chardriver_announce();
    }

    /* Initialization completed successfully. */
    return OK;
}

int main(void)
{
    /*
     * Perform initialization.
     */
    sef_local_startup();

    /*
     * Run the main loop.
     */
    chardriver_task(&poly_list_tab, CHARDRIVER_SYNC);
    return OK;
}