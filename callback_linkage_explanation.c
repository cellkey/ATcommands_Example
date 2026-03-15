/**
 * @file callback_linkage_explanation.c
 * @brief COMPLETE EXPLANATION: How typedef connects to storage and usage
 * 
 * This explains the "missing link" between:
 * 1. typedef declaration
 * 2. storage variable
 * 3. actual usage
 */

#include <stdio.h>
#include <stddef.h>

// ============================================================================
// STEP 1: TYPEDEF DECLARATION (What you have in Modem_Config_Handling.h:78)
// ============================================================================

/**
 * This typedef creates a NEW TYPE called 'modem_data_received_cb_t'
 * 
 * Think of it like this:
 * - 'int' is a type that can hold integers
 * - 'char*' is a type that can hold string pointers
 * - 'modem_data_received_cb_t' is a type that can hold function pointers
 * 
 * The function pointer must match this signature:
 * - Returns: void (nothing)
 * - Takes: const char *data, size_t length
 */
typedef void (*modem_data_received_cb_t)(const char *data, size_t length);

// ============================================================================
// STEP 2: STORAGE - Using the typedef as a TYPE (Like declaring int x = 0)
// ============================================================================

/**
 * This declares a VARIABLE of the type we just created
 * 
 * Just like:
 * - int my_number = 42;              (my_number is a variable of type 'int')
 * - char *my_string = "hello";       (my_string is a variable of type 'char*')
 * - modem_data_received_cb_t data_callback = NULL;  (data_callback is a variable of type 'modem_data_received_cb_t')
 * 
 * The variable 'data_callback' can store the ADDRESS of any function (pointer to function)
 * that matches the signature: void function_name(const char *data, size_t length)
 */
static modem_data_received_cb_t data_callback = NULL;

// ============================================================================
// STEP 3: THE LINKAGE - How they connect
// ============================================================================

/**
 * @brief This is a REAL function that matches the typedef signature
 * Notice: void return, const char *data, size_t length parameters
 */
void my_actual_callback_function(const char *data, size_t length) {
    printf("Callback called with: %s (length: %zu)\n", data, length);
}

/**
 * @brief Another function with the SAME signature
 */
void another_callback_function(const char *data, size_t length) {
    printf("Different callback: %s\n", data);
}

/**
 * @brief This function shows HOW THE LINKAGE WORKS
 */
void demonstrate_linkage(void) {
    printf("=== DEMONSTRATING THE LINKAGE ===\n\n");

    // STEP A: Initially, data_callback is NULL (no function assigned)
    printf("1. Initial state:\n");
    printf("   data_callback = %p (NULL)\n", (void*)data_callback);
    
    if (data_callback == NULL) {
        printf("   ✓ No callback function assigned yet\n");
    }
    printf("\n");

    // STEP B: Assign a function to data_callback (THE LINKAGE!)
    printf("2. Assigning function to callback:\n");
    data_callback = my_actual_callback_function;  // <-- THIS IS THE LINKAGE!
    printf("   data_callback = %p (now points to my_actual_callback_function)\n", (void*)data_callback);
    printf("   ✓ data_callback now 'points to' my_actual_callback_function\n");
    printf("\n");

    // STEP C: Use the callback (call the function through the pointer)
    printf("3. Calling the function through data_callback:\n");
    if (data_callback != NULL) {
        printf("   Calling: data_callback(\"Hello\", 5);\n");
        data_callback("Hello", 5);  // <-- This calls my_actual_callback_function!
        printf("   ✓ Function was called successfully!\n");
    }
    printf("\n");

    // STEP D: Change which function the callback points to
    printf("4. Changing to a different function:\n");
    data_callback = another_callback_function;  // <-- Change the linkage!
    printf("   data_callback now points to another_callback_function\n");
    
    printf("   Calling: data_callback(\"World\", 5);\n");
    data_callback("World", 5);  // <-- Now this calls another_callback_function!
    printf("   ✓ Different function was called!\n");
    printf("\n");

    // STEP E: Clear the callback
    printf("5. Clearing the callback:\n");
    data_callback = NULL;  // <-- Break the linkage
    printf("   data_callback = NULL (no function assigned)\n");
    
    if (data_callback == NULL) {
        printf("   ⚠️  Calling data_callback now would crash! (NULL pointer)\n");
    }
}

// ============================================================================
// STEP 4: REAL EXAMPLE - How this works in YOUR modem code
// ============================================================================

/**
 * @brief This simulates what happens in your Modem_Config_Handling.c
 */
void simulate_your_modem_code(void) {
    printf("=== SIMULATING YOUR MODEM CODE ===\n\n");

    // This is like modem_register_data_callback() in your code
    printf("1. User registers their callback:\n");
    printf("   modem_register_data_callback(my_actual_callback_function);\n");
    data_callback = my_actual_callback_function;  // Store user's function
    printf("   ✓ User's function is now stored in data_callback\n");
    printf("\n");

    // This simulates what happens when TCP data arrives (like in your modem task)
    printf("2. TCP data arrives from server:\n");
    const char *received_data = "SENSOR_DATA:25.3";
    size_t data_length = 16;
    
    printf("   Modem received: \"%s\"\n", received_data);
    printf("   Modem checks: if (data_callback != NULL)\n");
    
    if (data_callback != NULL) {
        printf("   ✓ Callback is registered, calling user's function...\n");
        printf("   Executing: data_callback(\"%s\", %zu);\n", received_data, data_length);
        data_callback(received_data, data_length);  // Call user's function!
        printf("   ✓ User's function processed the data!\n");
    } else {
        printf("   ⚠️  No callback registered, data ignored\n");
    }
}

// ============================================================================
// STEP 5: COMPLETE FLOW VISUALIZATION
// ============================================================================

void visualize_complete_flow(void) {
    printf("\n=== COMPLETE FLOW VISUALIZATION ===\n");
    printf("\n");
    printf("TYPEDEF:    typedef void (*modem_data_received_cb_t)(const char *data, size_t length);\n");
    printf("            ↓\n");
    printf("            Creates a new TYPE that can hold function pointers\n");
    printf("\n");
    printf("STORAGE:    static modem_data_received_cb_t data_callback = NULL;\n");
    printf("            ↓\n");
    printf("            Creates a VARIABLE of that type (initially empty/NULL)\n");
    printf("\n");
    printf("LINKAGE:    data_callback = my_actual_callback_function;\n");
    printf("            ↓\n");
    printf("            CONNECTS the variable to a real function\n");
    printf("            (The function ADDRESS is stored in the variable)\n");
    printf("\n");
    printf("USAGE:      if (data_callback != NULL) {\n");
    printf("                data_callback(\"data\", 4);  // Calls the linked function!\n");
    printf("            }\n");
    printf("            ↓\n");
    printf("            The stored function is executed\n");
    printf("\n");
    printf("ANALOGY:\n");
    printf("  typedef     = Create a new type of 'container'\n");
    printf("  storage     = Create an actual 'container' (initially empty)\n");
    printf("  linkage     = Put something IN the container\n");
    printf("  usage       = Use what's in the container\n");
}

// ============================================================================
// MAIN DEMONSTRATION
// ============================================================================

int main(void) {
    demonstrate_linkage();
    simulate_your_modem_code();
    visualize_complete_flow();
    
    printf("\n=== KEY INSIGHT ===\n");
    printf("The 'typedef' creates a TYPE.\n");
    printf("The 'static' creates a VARIABLE of that type.\n");
    printf("The '=' assignment creates the LINKAGE.\n");
    printf("The function call USES the linkage.\n");
    printf("\nIt's like: typedef creates a blueprint, static creates a box,\n");
    printf("= puts something in the box, () uses what's in the box!\n");
    
    return 0;
}

/* 
 * COMPILE AND RUN:
 * gcc callback_linkage_explanation.c -o linkage
 * ./linkage
 * 
 * This will show you EXACTLY how the typedef connects to storage and usage!
 */