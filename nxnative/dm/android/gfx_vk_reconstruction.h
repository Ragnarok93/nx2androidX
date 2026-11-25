#pragma once

// A complete physical color image captured after replay's last certified
// write. The variant is retained independently of the guest cache owner.
typedef struct {
    vk_rs_variant *backing;
    VkExtent2D physical, logical;
    VkRect2D valid;
    VkImageView sampled;
    VkFormat format; // includes the color encoding
    VkImageLayout layout;
    uint64_t ticket;
    int flip_y;
} vk_reconstruction_input;
