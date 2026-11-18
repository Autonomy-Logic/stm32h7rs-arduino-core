# STM32H7S7 core development signing key

`stm32h7rs-dev-ecdsa-p256.pem` is a **development key, published on purpose**. MCUboot on devices built with this
core checks that every program image is signed with it.

- It proves **integrity, not authenticity**: anyone can read this key and sign an image the device will accept. It
  only rejects images that were corrupted or altered without re-signing.
- It is for the **proof of concept only** and must **never be used for production units**. A production build needs
  a private key kept out of this repository and a signing step that users cannot run themselves.
