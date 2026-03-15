/**
 * @file your_callback_linkage_explained.c
 * @brief THE MISSING LINK - How typedef connects to storage in YOUR code
 * 
 * This shows EXACTLY how the linkage works in YOUR Modem_Config_Handling files
 */

// ============================================================================
// FROM YOUR Modem_Config_Handling.h (line 78) - THE TYPEDEF
// ============================================================================

/**
 * STEP 1: This line CREATES A NEW TYPE called 'modem_data_received_cb_t'
 * 
 * Before this line: 'modem_data_received_cb_t' doesn't exist
 * After this line:  'modem_data_received_cb_t' is a TYPE (like 'int' or 'char*')
 * 
 * What it means: "Any variable of type 'modem_data_received_cb_t' can store
 *                 the address of a function that takes (const char*, size_t) 
 *                 and returns void"
 */
typedef void (*modem_data_received_cb_t)(const char *data, size_t length);

// ============================================================================
// FROM YOUR Modem_Config_Handling.c (line 35) - THE STORAGE
// ============================================================================

/**
 * STEP 2: This line CREATES A VARIABLE using the type we just defined
 * 
 * Just like:  int my_number = 0;
 * This does:  modem_data_received_cb_t data_callback = NULL;
 * 
 * 'data_callback' is now a CONTAINER that can hold a function address
 * Initially it's NULL (empty container)
 */
static modem_data_received_cb_t data_callback = NULL;

// ============================================================================
// FROM YOUR Modem_Config_Handling.c (line 362-363) - THE LINKAGE!
// ============================================================================

/**
 * STEP 3: This is WHERE THE MAGIC HAPPENS - The Registration Function
 * 
 * This function takes a parameter called 'callback' which is ALSO of type
 * 'modem_data_received_cb_t' (a function pointer)
 * 
 * Line 363: data_callback = callback;
 * 
 * THIS IS THE LINKAGE! It stores the user's function address into our variable
 */
void modem_register_data_callback(modem_data_received_cb_t callback) {
    data_callback = callback;  // <-- THE LINKAGE HAPPENS HERE!
}

// ============================================================================
// THE COMPLETE FLOW - Step by step breakdown
// ============================================================================

void demonstrate_your_code_linkage(void) {
    printf("=== YOUR CODE LINKAGE EXPLAINED ===\n\n");

    // What happens when user writes their callback function:
    printf("1. USER WRITES THEIR FUNCTION:\n");
    printf("   void my_data_handler(const char *data, size_t length) {\n");
    printf("       printf(\"Got data: %%s\\n\", data);\n");
    printf("   }\n\n");

    // What happens when user calls registration:
    printf("2. USER REGISTERS THEIR FUNCTION:\n");
    printf("   modem_register_data_callback(my_data_handler);\n");
    printf("   ↓\n");
    printf("   This calls YOUR function with 'callback = my_data_handler'\n");
    printf("   ↓\n");
    printf("   Line 363: data_callback = callback;\n");
    printf("   ↓\n");
    printf("   Now data_callback POINTS TO my_data_handler\n");
    printf("   ✓ LINKAGE ESTABLISHED!\n\n");

    // What happens when your modem code wants to call the callback:
    printf("3. YOUR MODEM CODE USES THE CALLBACK:\n");
    printf("   // Somewhere in your modem task when data arrives:\n");
    printf("   char received_data[] = \"HELLO\";\n");
    printf("   if (data_callback != NULL) {           // Check if linked\n");
    printf("       data_callback(received_data, 5);   // Call the linked function!\n");
    printf("   }\n");
    printf("   ↓\n");
    printf("   This actually calls my_data_handler(\"HELLO\", 5)\n");
    printf("   ✓ USER'S FUNCTION GETS EXECUTED!\n\n");
}

// ============================================================================
// VISUAL ANALOGY - To make it crystal clear
// ============================================================================

void visual_analogy(void) {
    printf("=== VISUAL ANALOGY ===\n\n");
    
    printf("Think of it like a PHONE SYSTEM:\n\n");
    
    printf("TYPEDEF:     Creates a 'phone number format' (must be 10 digits)\n");
    printf("             typedef void (*modem_data_received_cb_t)(...)\n");
    printf("             ↓\n");
    printf("             \"Any phone number must match this format\"\n\n");
    
    printf("STORAGE:     Creates an actual 'phone book entry' (initially empty)\n");
    printf("             static modem_data_received_cb_t data_callback = NULL;\n");
    printf("             ↓\n");
    printf("             Phone Book: [data_callback] = (empty)\n\n");
    
    printf("REGISTRATION: User gives you their phone number\n");
    printf("             modem_register_data_callback(my_data_handler);\n");
    printf("             ↓\n");
    printf("             data_callback = callback;  // Store the number!\n");
    printf("             ↓\n");
    printf("             Phone Book: [data_callback] = 555-1234 (my_data_handler)\n\n");
    
    printf("USAGE:       When you need to call them\n");
    printf("             if (data_callback != NULL) {\n");
    printf("                 data_callback(\"data\", 4);  // Dial the number!\n");
    printf("             }\n");
    printf("             ↓\n");
    printf("             Their phone rings! (my_data_handler gets called)\n\n");
}

// ============================================================================
// THE EXACT MEMORY ADDRESSES - What really happens
// ============================================================================

// Example user function
void example_user_function(const char *data, size_t length) {
    printf("User function called with: %s\n", data);
}

void show_memory_addresses(void) {
    printf("=== WHAT HAPPENS IN MEMORY ===\n\n");
    
    printf("1. INITIAL STATE:\n");
    printf("   data_callback = %p (NULL)\n", (void*)data_callback);
    printf("   example_user_function address = %p\n", (void*)example_user_function);
    printf("\n");
    
    printf("2. AFTER REGISTRATION:\n");
    printf("   modem_register_data_callback(example_user_function);\n");
    data_callback = example_user_function;  // This is what line 363 does!
    printf("   data_callback = %p (now same as function address!)\n", (void*)data_callback);
    printf("   ✓ They point to the same memory location!\n");
    printf("\n");
    
    printf("3. WHEN CALLBACK IS USED:\n");
    printf("   data_callback(\"test\", 4);  // This calls the function at that address\n");
    if (data_callback) {
        data_callback("test", 4);
    }
    printf("   ✓ Function executed!\n");
}

// ============================================================================
// MAIN DEMONSTRATION
// ============================================================================

int main(void) {
    demonstrate_your_code_linkage();
    visual_analogy();
    show_memory_addresses();
    
    printf("\n=== THE MISSING LINK REVEALED ===\n");
    printf("The 'typedef' is like defining a standard plug shape.\n");
    printf("The 'static' creates an electrical socket of that shape.\n");
    printf("The 'registration' plugs a device into the socket.\n");
    printf("The 'usage' turns on the power to run the plugged device!\n");
    printf("\nNow you see the complete connection! 🔌\n");
    
    return 0;
}

/*
 * KEY INSIGHT FOR YOUR CODE:
 * 
 * Modem_Config_Handling.h:78  → typedef creates the TYPE
 * Modem_Config_Handling.c:35  → static creates a VARIABLE of that type  
 * Modem_Config_Handling.c:363 → data_callback = callback; (THE LINKAGE!)
 * 
 * When your modem receives data, it can call: data_callback("data", length);
 * This will execute whatever function the user registered!
 */