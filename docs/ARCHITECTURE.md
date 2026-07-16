# Architecture

    ugreenctl CLI
        |
        +-- core/       option parsing, DMI selection, dynamic plugin loader
        |
        +-- models/     one shared object per NAS model
                |
                +-- fan/       verified fan operations
                +-- power/     verified AC recovery operations
                +-- led/       LED API; no verified map currently
                +-- ec/        EC transport detection; no generic writes
                +-- superio/   ITE Super I/O transport

The core owns model matching, dry-run write protection, and plugin ABI
validation. A model plugin owns its hardware map. Shared transport code must
not make a controller usable until a model explicitly enables it.
