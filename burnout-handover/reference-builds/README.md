# Reference builds — not replacements for latest build/

reported-failure-v2-soft/ is the preserved v2 Soft build associated with the last
user-reported Burnout crash. PRX, matching ELF and full source/build ZIP are
included. Its module/log banner still says simple_r28; banner alone does not
identify an artifact. The association comes from conversation timing, not a
SHA256 read from the physical PSP. Keep this uncertainty explicit.

R21 memory-test ZIP is the build with a user-confirmed Complete success in
native2MiB, user workspace retained, no scaler jobs. Its earlier stages also
allowed fully freed native. Read source and associated log rather than assuming
which button action happened.
R22 EDRAM-test ZIP contains the proposed native4MiB/no-jobs comparison. No
confirmed device result was supplied for that comparison.
No game content or save state is included. Do not combine source/ELF/PRX across
builds. Root SHA256SUMS covers these preserved artifacts too.
