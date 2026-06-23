# Third-Party License Inventory

Status checked: 2026-06-21.

**EXTERNAL DISTRIBUTION: BLOCKED**

This is an engineering gate, not legal advice. Composer must not publish executables, source
packages, or bundled content until the repository owner records either an AGPLv3-compliant release
decision or a valid commercial JUCE 8 license and completes the S4 dependency review.

| Dependency | Pin | License evidence | S0 use |
|---|---|---|---|
| JUCE 8.0.13 | commit `7c9d3783b127263d72bb65fe0a7e2dc8a02a7ac2`; archive SHA-256 `97c3c5cf039d8ba45378397c3d6c1033c3fc85102c928054a77e8857031ecae3` | upstream `LICENSE.md`: AGPLv3 or commercial JUCE 8 license | core, audio, devices, GUI, tests |
| CMake 4.3.3 | optional local tool archive SHA-256 `935ade9e5e8723583c07f44c5592cea2a1c8f65c56ca7e07b34c025c880e0bd6` | BSD-3-Clause upstream | build tool only |
| Ninja 1.13.2 | optional local tool archive SHA-256 `07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65` | Apache-2.0 upstream | build tool only |

Official JUCE references:

- https://github.com/juce-framework/JUCE/blob/8.0.13/LICENSE.md
- https://juce.com/legal/juce-8-licence/

No sample assets are bundled in S0. ASIO is disabled in the current target, so the proprietary
Steinberg ASIO SDK path is not part of the S0 binary. Transitive licenses for JUCE modules used by
the eventual release must be copied and verified in S4 before the block can be removed.
