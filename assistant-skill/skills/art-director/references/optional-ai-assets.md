# Optional Generated Assets — v0.7.0

AI asset generation is a separate, permission-gated workflow. It is not a default method of improving an edit.

## Preferred order

1. Search the user's project footage and licensed asset library.
2. Prefer existing Dafin titles, native shapes, Lottie, effects and transitions when they communicate the same idea.
3. If something illustrative is genuinely missing, call `dafin_plan_optional_asset(request={...})` for a **proposal only**.
4. Native Dafin UI obtains the user's explicit decision about a specific provider/model, destination, uploaded data, terms, licensing, quoted cost and output limits.
5. A separately implemented, isolated native adapter may then run an authorized job; generated output is previewed and must be approved before any timeline insertion.

## Never do these

- Present generated footage as authentic documentary evidence, experiment results or real reactions.
- Upload project/media bytes to a remote provider without separate informed consent.
- Treat a model-provided `user_approved: true` value as permission. Permission comes from trusted native UI; the MCP planning schema deliberately has no approval field.
- Override `no_generated_assets`, `no_cloud_uploads` or source-asset rights.
- Claim any provider has been configured, generation completed or a cost estimated from this prototype. Its calls are read-only, with zero network/provider calls.

The projects at `Anil-matcha/Open-Generative-AI` and `Open-Generative-AI/youtube-rag-chatbot` provide architectural inspiration only; neither project's code or transcripts are bundled.
