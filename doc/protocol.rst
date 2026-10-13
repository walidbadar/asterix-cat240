Protocol
========

ASTERIX (All Purpose STructured Eurocontrol suRveillance Information EXchange)
Category 240 carries radar video: the amplitude of the echo received along one
radial, sampled in range cells, together with the azimuth and range geometry
needed to place those cells. The format is defined in
`EUROCONTROL-SPEC-0149-240`_, edition 1.3.

A data block is one CAT octet (240), a two octet big endian length covering the
whole block, and one or more records. A record is a Field Specification (FSPEC)
naming the data items present, followed by the items in Field Reference Number
order. Two message types exist:

* A **video summary message** carries free text describing the stream
  (I240/030).
* A **video message** carries one radial: the video header with the cell
  duration in nanoseconds (I240/040) or femtoseconds (I240/041), the cell
  resolution and compression flag (I240/048), the valid octet and cell counters
  (I240/049) and the cell amplitudes in one of three video block items
  (I240/050, I240/051, I240/052), which differ only in their block size of 4,
  64 or 256 octets.

Both types may carry the time of day (I240/140) and the Reserved Expansion and
Special Purpose fields.

.. _EUROCONTROL-SPEC-0149-240:
   https://www.eurocontrol.int/sites/default/files/content/documents/nm/asterix/20150513-asterix-cat240-v1.3.pdf
