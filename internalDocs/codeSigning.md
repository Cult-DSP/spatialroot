# some unexplored options

https://github.com/sigstore/cosign

https://ossign.org/

https://github.com/sigstore/fulcio

sign path foundation

cosign + fulcio (Sigstore)

Free, keyless signing via OIDC (GitHub, Google, Microsoft identity)
Signs artifacts (binaries, containers, etc.) with short-lived certs anchored to your identity
Verifiable via the public Rekor transparency log
Does NOT satisfy macOS Gatekeeper or Windows SmartScreen — Apple and Microsoft only trust certs from their own CAs
ossign.org

I'm less familiar with this one specifically; I'd recommend reading their docs directly rather than me guessing
For macOS (what you likely need for spatialroot):

No free path to Apple-trusted signing — Apple Developer Program is $99/year, required for notarization
SignPath Foundation (signpath.io/foundation) offers free Authenticode (Windows) signing for FOSS projects
Certum has offered discounted open-source certs in the past
Practical summary:

Use Sigstore/cosign if you want cryptographic artifact signing for your release pipeline (great for Linux/container users who want to verify downloads)
For macOS distribution without Gatekeeper warnings, the Apple Developer account is essentially unavoidable
