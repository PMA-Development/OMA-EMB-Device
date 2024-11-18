# User generation for EMQX
This generation scripts creates userses to import into EMQX, and a json file to copy into appsettings.json, to use with the device simulator

## How to use
The script creates 10 users as default. To change the quantity of users, please open the script, and change the variable "users_to_create". Else just rung the script by: python3 ./generate_users.py