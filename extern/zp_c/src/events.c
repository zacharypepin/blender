#include "zp_c/events.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "zp_c/fatal.h"

typedef struct event_subscription_zt
{
    event_sub_id_zt subscription_id;
    event_callback_t callback;
    void* user_data;
    struct event_subscription_zt* next;
} event_subscription_zt;

struct event_zt
{
    event_subscription_zt* subscriptions;
    event_sub_id_zt next_subscription_id;
};

// =========================================================================================================================================
// =========================================================================================================================================
// event_init_z: Initialize a new event instance. Allocates and initializes the event structure.
// =========================================================================================================================================
// =========================================================================================================================================
event_zh event_init_z(void)
{
    // =============================================================================================
    // =============================================================================================
    // Allocate and initialize event structure.
    // =============================================================================================
    // =============================================================================================
    event_zh event;
    {
        event = fatal_alloc_z(sizeof(struct event_zt), "failed to allocate event");
        memset(event, 0, sizeof(struct event_zt));
        event->next_subscription_id = 1;
    }

    return event;
}

// =========================================================================================================================================
// =========================================================================================================================================
// event_destroy_z: Destroy an event instance and free all associated memory including all subscriptions.
// =========================================================================================================================================
// =========================================================================================================================================
void event_destroy_z(event_zh event)
{
    // =============================================================================================
    // =============================================================================================
    // Validate event state.
    // =============================================================================================
    // =============================================================================================
    {
        fatal_check_z(event, "event is null");
    }

    // =============================================================================================
    // =============================================================================================
    // Free all subscriptions.
    // =============================================================================================
    // =============================================================================================
    {
        event_subscription_zt* current = event->subscriptions;
        while (current != nullptr)
        {
            event_subscription_zt* next = current->next;
            free(current);
            current = next;
        }
    }

    // =============================================================================================
    // =============================================================================================
    // Free event structure.
    // =============================================================================================
    // =============================================================================================
    {
        free(event);
    }
}

// =========================================================================================================================================
// =========================================================================================================================================
// event_subscribe_z: Subscribe a callback to an event. The callback will be invoked when the event is triggered.
// The user_data pointer will be passed to the callback when invoked. Returns a subscription ID that can be used to unsubscribe.
// =========================================================================================================================================
// =========================================================================================================================================
event_sub_id_zt event_subscribe_z(event_zh event, event_callback_t callback, void* user_data)
{
    // =============================================================================================
    // =============================================================================================
    // Validate parameters.
    // =============================================================================================
    // =============================================================================================
    {
        fatal_check_z(event, "event is null");
        fatal_check_z(callback, "callback is null");
    }

    // =============================================================================================
    // =============================================================================================
    // Allocate and initialize subscription.
    // =============================================================================================
    // =============================================================================================
    event_subscription_zt* subscription;
    event_sub_id_zt subscription_id;
    {
        subscription                  = fatal_alloc_z(sizeof(event_subscription_zt), "failed to allocate subscription");
        subscription_id               = event->next_subscription_id++;
        subscription->subscription_id = subscription_id;
        subscription->callback        = callback;
        subscription->user_data       = user_data;
        subscription->next            = nullptr;
    }

    // =============================================================================================
    // =============================================================================================
    // Add subscription to the list.
    // =============================================================================================
    // =============================================================================================
    {
        if (event->subscriptions == nullptr)
        {
            event->subscriptions = subscription;
        }
        else
        {
            event_subscription_zt* current = event->subscriptions;
            while (current->next != nullptr)
            {
                current = current->next;
            }
            current->next = subscription;
        }
    }

    return subscription_id;
}

// =========================================================================================================================================
// =========================================================================================================================================
// event_unsubscribe_z: Unsubscribe a callback from an event using the subscription ID returned from event_subscribe_z.
// =========================================================================================================================================
// =========================================================================================================================================
void event_unsubscribe_z(event_zh event, event_sub_id_zt subscription_id)
{
    // =============================================================================================
    // =============================================================================================
    // Validate parameters.
    // =============================================================================================
    // =============================================================================================
    {
        fatal_check_z(event, "event is null");
    }

    // =============================================================================================
    // =============================================================================================
    // Find and remove subscription.
    // =============================================================================================
    // =============================================================================================
    {
        if (event->subscriptions == nullptr)
        {
            return;
        }

        // Check if it's the first subscription
        if (event->subscriptions->subscription_id == subscription_id)
        {
            event_subscription_zt* to_remove = event->subscriptions;
            event->subscriptions             = event->subscriptions->next;
            free(to_remove);
            return;
        }

        // Search for the subscription
        event_subscription_zt* current = event->subscriptions;
        while (current->next != nullptr)
        {
            if (current->next->subscription_id == subscription_id)
            {
                event_subscription_zt* to_remove = current->next;
                current->next                    = current->next->next;
                free(to_remove);
                return;
            }
            current = current->next;
        }
    }
}

// =========================================================================================================================================
// =========================================================================================================================================
// event_trigger_z: Trigger an event, invoking all subscribed callbacks with the provided event_data pointer.
// Callbacks are invoked in the order they were subscribed. The event_data pointer is passed directly to callbacks.
// =========================================================================================================================================
// =========================================================================================================================================
void event_trigger_z(event_zh event, const void* event_data)
{
    // =============================================================================================
    // =============================================================================================
    // Validate parameters.
    // =============================================================================================
    // =============================================================================================
    {
        fatal_check_z(event, "event is null");
    }

    // =============================================================================================
    // =============================================================================================
    // Invoke all subscribed callbacks. Create a copy of the subscription list to avoid issues
    // if callbacks modify the subscription list during iteration.
    // =============================================================================================
    // =============================================================================================
    {
        // Build a temporary list of callbacks to invoke
        event_subscription_zt* current = event->subscriptions;
        size_t count                   = 0;
        while (current != nullptr)
        {
            count++;
            current = current->next;
        }

        if (count == 0)
        {
            return;
        }

        // Allocate array to store callbacks and user_data
        struct
        {
            event_callback_t callback;
            void* user_data;
        }* callbacks = fatal_alloc_z(count * sizeof(*callbacks), "failed to allocate callback array");

        // Copy callbacks to array
        current      = event->subscriptions;
        size_t index = 0;
        while (current != nullptr)
        {
            callbacks[index].callback  = current->callback;
            callbacks[index].user_data = current->user_data;
            index++;
            current = current->next;
        }

        // Invoke all callbacks
        for (size_t i = 0; i < count; i++)
        {
            callbacks[i].callback(event_data, callbacks[i].user_data);
        }

        free(callbacks);
    }
}
