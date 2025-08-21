
#ifndef SIMPLE_LIST_H
#define SIMPLE_LIST_H

#include <cstddef> // for std::size_t
#include <initializer_list>
#include <iostream> // for std::cout

/*
Quick note on perfect forwarding and variadic templates:
template<typename... Args>
void demo(Args&&... args) {
    call(args...); // expands as call(arg1, arg2, ...)
    process(std::forward<Args>(args)...); // forwards each arg
    int arr[] = { (log(args), 0)... }; // calls log(arg1), log(arg2), ...
    using Tuple = std::tuple<Args...>; // expands into tuple<T1, T2, T3, ...>
}
*/

// dList is a doubly linked list implementation for generic data where each node is represented by dNode.
template <typename T>
class dList {
private:

    // struct dNode represents each node of the list.
    // The dNode struct contains data, a pointer to the next node, and a pointer to the previous node.
    // Make this a private member so that it cannot be accessed directly from outside the class.
    // This encapsulation ensures that the list's integrity is maintained and operations on nodes are controlled
    // through the public methods of dList.
    // The dNode struct is templated to allow for any data type T to be stored in the list.
    struct dNode {
        T data;
        dNode* next;
        dNode *prev;

        // Constuctors for each list dNode
        dNode() : next(nullptr), prev(nullptr) {}
        explicit dNode(const T& value) : data(value), next(nullptr), prev(nullptr) {}

        // Constructor that allows perfect forwarding of arguments to construct data in place.
        // This is useful for types that are expensive to copy or move.
        template <typename... Args>
        dNode(Args&&... args) : data(std::forward<Args>(args)...),next(nullptr), prev(nullptr)
        {
            // This code invokes the default constructor of T then the assignment operator
            //Whreas using constructor initialization list allows us to directly construct the data member.
            // This is less efficient so commented it out.
            /*
            data = T(std::forward<Args>(args)...);
            next = nullptr;
            prev = nullptr;  
            */ 
        }
    };

public:
    dNode* dlHead;
    dNode* dlTail;
    std::size_t dlSize;

    // Constructor for dList initializes head and tail to nullptr and size to 0.
    dList() : dlHead(nullptr),dlTail(nullptr), dlSize(0){}

    dList(std::initializer_list<T>  otherList) 
    {
        dlHead=nullptr;
        dlTail=nullptr;
        dlSize=0;
        for ( auto& value : otherList) {
            push_back(std::move(value));
        }
    }

    // Disable copy constructor and assignment operator to prevent copying of the list.
    dList(const dList&) = delete;
    dList& operator=(const dList&) = delete;

    dNode* head() const {
        return dlHead;
    }

    dNode* tail() const {
        return dlTail;
    }

    std::size_t size() const {
        return dlSize;
    }

    bool empty() const {
        return dlSize == 0;
    }

    // Create a new node in place, at the end of the list with the given value
    void push_back(const T& value)
    {
        emplace_back(value);
    }

    // insert a new node where the value is of rvalue type, so it can ve safely moved.  
    void push_back(T&& value)
    {
        emplace_back(std::move(value));
    }

    T pop_back()
    {
        if (dlTail == nullptr) {
            throw std::out_of_range("Cannot pop from an empty list");
            return 0; // List is empty, nothing to pop
        }   
        dNode* nodeToPop = dlTail;
        dlTail = dlTail->prev;
        if (dlTail) {
            dlTail->next = nullptr; // Update the new tail's next pointer
        } else {
            dlHead = nullptr; // If the list is now empty, update head
        }
        dlSize--;

        T dataToReturn = std::move(nodeToPop->data); // Store the data to return
        delete nodeToPop; // Delete the popped node 
        return dataToReturn; // Return the popped node
    }

    // Insert a new node at given position in the list
    void insert(const T& value, dNode* position)
    {
        if (!position) {
            push_back(value);
            return;
        }

        dNode* newNode = new dNode(value);
        newNode->next = position;
        newNode->prev = position->prev;
        if (position->prev)
            position->prev->next = newNode;
        else
            dlHead = newNode;
        position->prev = newNode;
        dlSize++;
    }

    void insert(dNode* position,T&& value)
    {
        if (!position) {
            push_back(std::move(value));
            return;
        }

        dNode* newNode = new dNode(std::move(value));
        newNode->next = position;
        newNode->prev = position->prev;
        if (position->prev)
            position->prev->next = newNode;
        else
            dlHead = newNode;
        position->prev = newNode;
        dlSize++;
    }

    // Construct a new node in-place and insert at the end of the list.
    template <typename... Args>
    void emplace_back(Args&&... args)
    {
        // This function allows for perfect forwarding of arguments to construct the data in place.
        dNode* newNode = new dNode(std::forward<Args>(args)...);
        if (dlTail){
            dlTail->next = newNode;
            newNode->prev = dlTail;
            dlTail = newNode;
        }
        else {
            dlHead = dlTail = newNode;
        }
        dlSize++;
    }

    // Construct a new node in-place and insert at the given position.
    template <typename... Args>
    void emplace(dNode* position,Args&&... args)
    {
        // Using perfect forwarding here to create object of type T in place.
        if (!position){
            emplace_back(std::forward<Args>(args)...);
            return;
        }
        dNode* newNode = new dNode(std::forward<Args>(args)...);
        newNode->next = position;
        newNode->prev = position->prev;
        if (position->prev)
            position->prev->next = newNode;
        else {
            dlHead = newNode;
        }
        position->prev = newNode;
        dlSize++;
    }

    // Erase a node at the given position.
    void erase(dNode* position)
    {
        if (!position) return;

        if (position->prev)
            position->prev->next = position->next;
        else
            dlHead = position->next;

        if (position->next)
            position->next->prev  = position->prev;
        else
            dlTail = position->prev;

        delete position;
        dlSize--;
        
    }

    // TODO: Need to refine this as T can be a non-printable type 
#ifdef DEBUG
    void print() const {
        dNode* current = dlHead;
        while (current) {
            std::cout << current->data << " ";
            current = current->next;
        }
        std::cout << std::endl;

        // Explore SFINAE-restrict for providing print functionality for non-printable types
    }
#endif // DEBUG
    // Clear the list by deleting all nodes
    void clearList()
    {
        dNode* delNode = dlHead;
        while(delNode)
        {
            dlHead = delNode->next;
            delete delNode;
            delNode = dlHead;
        }
        dlHead = nullptr;
        dlTail = nullptr;
        dlSize = 0;
    }

    ~dList()
    {
        clearList();
    }
};

#endif // SIMPLE_LIST_H