#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bootstrap_transaction.h"

typedef struct {
    char order[8];
    unsigned int order_length;
    int payload_result;
    int pointer_result;
    int verify_result;
    const unsigned char *seen_payload;
    unsigned int seen_payload_size;
    uint32_t seen_start;
    uint32_t seen_size;
} fake_transport_t;

static void record(fake_transport_t *fake, char step)
{
    assert(fake->order_length + 1 < sizeof(fake->order));
    fake->order[fake->order_length++] = step;
    fake->order[fake->order_length] = '\0';
}

static int fake_write_payload(void *context, const unsigned char *payload,
                              unsigned int payload_size, uint32_t start_sector)
{
    fake_transport_t *fake = context;

    record(fake, 'P');
    fake->seen_payload = payload;
    fake->seen_payload_size = payload_size;
    fake->seen_start = start_sector;
    return fake->payload_result;
}

static int fake_set_pointer(void *context, uint32_t start, uint32_t size)
{
    fake_transport_t *fake = context;

    record(fake, 'S');
    fake->seen_start = start;
    fake->seen_size = size;
    return fake->pointer_result;
}

static int fake_verify_pointer(void *context,
                               unsigned char destination[APA_HEADER_SIZE],
                               uint32_t expected_start,
                               uint32_t expected_size)
{
    fake_transport_t *fake = context;

    record(fake, 'V');
    fake->seen_start = expected_start;
    fake->seen_size = expected_size;
    if (fake->verify_result == 0)
        destination[0] = 0xa5;
    return fake->verify_result;
}

static void fake_release(void *context)
{
    record(context, 'R');
}

static bootstrap_transaction_ops_t fake_ops(fake_transport_t *fake)
{
    bootstrap_transaction_ops_t ops = {
        fake_write_payload,
        fake_set_pointer,
        fake_verify_pointer,
        fake
    };

    return ops;
}

static void test_pointer_success(void)
{
    fake_transport_t fake = {0};
    bootstrap_transaction_ops_t ops = fake_ops(&fake);
    bootstrap_transaction_result_t result;
    unsigned char header[APA_HEADER_SIZE] = {0};

    assert(bootstrap_transaction_set_pointer(&ops, header, 0x2000, 7,
                                             &result) == 0);
    assert(strcmp(fake.order, "SV") == 0);
    assert(fake.seen_start == 0x2000);
    assert(fake.seen_size == 7);
    assert(header[0] == 0xa5);
    assert(result.stage == BOOTSTRAP_TRANSACTION_STAGE_NONE);
    assert(result.code == 0);
}

static void test_pointer_failures_stop_immediately(void)
{
    fake_transport_t fake = {0};
    bootstrap_transaction_ops_t ops = fake_ops(&fake);
    bootstrap_transaction_result_t result;
    unsigned char header[APA_HEADER_SIZE] = {0};

    fake.pointer_result = -41;
    assert(bootstrap_transaction_set_pointer(&ops, header, 1, 2,
                                             &result) == -41);
    assert(strcmp(fake.order, "S") == 0);
    assert(result.stage == BOOTSTRAP_TRANSACTION_STAGE_POINTER_SET);
    assert(result.code == -41);

    memset(&fake, 0, sizeof(fake));
    ops = fake_ops(&fake);
    fake.verify_result = -42;
    assert(bootstrap_transaction_set_pointer(&ops, header, 1, 2,
                                             &result) == -42);
    assert(strcmp(fake.order, "SV") == 0);
    assert(result.stage == BOOTSTRAP_TRANSACTION_STAGE_POINTER_VERIFY);
    assert(result.code == -42);
}

static void test_activation_success_preserves_release_order(void)
{
    fake_transport_t fake = {0};
    bootstrap_transaction_ops_t ops = fake_ops(&fake);
    bootstrap_transaction_result_t result;
    unsigned char header[APA_HEADER_SIZE] = {0};
    const unsigned char payload[] = {1, 2, 3, 4};

    assert(bootstrap_transaction_activate(
               &ops, header, payload, sizeof(payload), 0x2000, 1,
               fake_release, &fake, &result) == 0);
    assert(strcmp(fake.order, "PRSV") == 0);
    assert(fake.seen_payload == payload);
    assert(fake.seen_payload_size == sizeof(payload));
    assert(fake.seen_start == 0x2000);
    assert(fake.seen_size == 1);
    assert(header[0] == 0xa5);
    assert(result.stage == BOOTSTRAP_TRANSACTION_STAGE_NONE);
}

static void test_payload_failure_never_exposes_pointer(void)
{
    fake_transport_t fake = {0};
    bootstrap_transaction_ops_t ops = fake_ops(&fake);
    bootstrap_transaction_result_t result;
    unsigned char header[APA_HEADER_SIZE] = {0};
    const unsigned char payload[] = {9, 8};

    fake.payload_result = -50;
    assert(bootstrap_transaction_activate(
               &ops, header, payload, sizeof(payload), 0x2000, 1,
               fake_release, &fake, &result) == -50);
    assert(strcmp(fake.order, "PR") == 0);
    assert(result.stage == BOOTSTRAP_TRANSACTION_STAGE_PAYLOAD);
    assert(result.code == -50);
}

static void test_post_payload_failures_keep_order(void)
{
    fake_transport_t fake = {0};
    bootstrap_transaction_ops_t ops = fake_ops(&fake);
    bootstrap_transaction_result_t result;
    unsigned char header[APA_HEADER_SIZE] = {0};
    const unsigned char payload[] = {7};

    fake.pointer_result = -60;
    assert(bootstrap_transaction_activate(
               &ops, header, payload, sizeof(payload), 0x2000, 1,
               fake_release, &fake, &result) == -60);
    assert(strcmp(fake.order, "PRS") == 0);
    assert(result.stage == BOOTSTRAP_TRANSACTION_STAGE_POINTER_SET);

    memset(&fake, 0, sizeof(fake));
    ops = fake_ops(&fake);
    fake.verify_result = -61;
    assert(bootstrap_transaction_activate(
               &ops, header, payload, sizeof(payload), 0x2000, 1,
               fake_release, &fake, &result) == -61);
    assert(strcmp(fake.order, "PRSV") == 0);
    assert(result.stage == BOOTSTRAP_TRANSACTION_STAGE_POINTER_VERIFY);
}

static void test_invalid_arguments_do_not_start_transaction(void)
{
    fake_transport_t fake = {0};
    bootstrap_transaction_ops_t ops = fake_ops(&fake);
    bootstrap_transaction_result_t result;
    unsigned char header[APA_HEADER_SIZE] = {0};

    assert(bootstrap_transaction_activate(
               &ops, header, NULL, 0, 1, 1,
               fake_release, &fake, &result) ==
           BOOTSTRAP_TRANSACTION_INVALID_ARGUMENT);
    assert(fake.order_length == 0);
    assert(result.stage == BOOTSTRAP_TRANSACTION_STAGE_NONE);
    assert(result.code == BOOTSTRAP_TRANSACTION_INVALID_ARGUMENT);
}

int main(void)
{
    test_pointer_success();
    test_pointer_failures_stop_immediately();
    test_activation_success_preserves_release_order();
    test_payload_failure_never_exposes_pointer();
    test_post_payload_failures_keep_order();
    test_invalid_arguments_do_not_start_transaction();
    puts("All bootstrap transaction ordering tests passed.");
    return 0;
}
