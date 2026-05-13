/**
 * Red Black Tree Hpp
 * by GTY
 * 2022.1
 * at Yushan County, Shangrao, Jiangxi
 */

// https://blog.csdn.net/m0_62405272/article/details/122612246
// https://blog.csdn.net/m0_62405272/article/details/122631653

// 2024.11.6: modified for Amkos


#include "../Allocator.h"
#include "../config.h"
#include "../sys/types.h"
#include <adl/utility>


static void __die() {
    printf("die RedBlackTree.hpp\n");
    while (1)
        ;
}


namespace adl {


template<typename K, typename V>
class RedBlackTreeIterator;


template <typename KeyType, typename DataType>
class RedBlackTree {
    friend RedBlackTreeIterator<KeyType, DataType>;

public:
    /** Object's life-management methods */
    RedBlackTree(adl::Allocator* = &adl::defaultAllocator);
    ~RedBlackTree();

    /**
     * Clear all elements in tree.
     */
    void clear();

public:
    /** Basic query methods */

    /**
     * Determine whether a key is in the tree.
     * 
     * @param queryKey
     */
    bool hasKey(const KeyType&);

    bool contains(const KeyType& key) {
        return hasKey(key);
    }

    /**
     * Get data (ref) by key.
     * 
     * @param key 
     */
    DataType& getData(const KeyType&);

    DataType& operator [] (const KeyType&);

    /**
     * Get data (clone) by key.
     */
    DataType copyData(const KeyType&, const DataType* fallback = nullptr);

    /**
     * Set data. If data with same key already exists, it would be overwritten.
     * 
     * @param key 
     * @param data 
     */
    RedBlackTree<KeyType, DataType>& setData(const KeyType&, const DataType&);

    /**
     * Delete key.
     * 
     * @param key
     */
    RedBlackTree<KeyType, DataType>& removeKey(const KeyType&, bool noExcept = true);

    adl::size_t size();

    /**
     * Range scan. This method will search for keys inside [lhs, rhs],
     * and calls collector with data for each node discovered.
     * 
     * @param lhs inclusive
     * @param rhs inclusive
     * @param data to be passed to collector
     * @param collector 
     * @return How many elements collected.
     */
    adl::size_t rangeScan(
        const KeyType& lhs, 
        const KeyType& rhs, 
        void* data,
        bool (*collector) (void* data, const KeyType&, const DataType&)
    );

    
    /* ------ iteration ------ */

    RedBlackTreeIterator<KeyType, DataType> begin() const {
        return RedBlackTreeIterator<KeyType, DataType>(this->root);
    }


    RedBlackTreeIterator<KeyType, DataType> end() const {
        return RedBlackTreeIterator<KeyType, DataType>(nullptr);
    }


public:



protected:
    enum class NodeColor {
        RED, BLACK
    };
    enum class ChildSide {
        LEFT, RIGHT
    };
    struct Node {
        KeyType key;
        DataType data;
        NodeColor color = NodeColor::RED;
        Node* parent = nullptr;
        Node* leftChild = nullptr;
        Node* rightChild = nullptr;
    };

protected:
    /**
     * Release one node with all of its children, recursively.
     * 
     * @param node first node to be released recursively
     */
    void cleanup(Node* node);


    Node* locateNode(const KeyType&);


    /**
     * Non-locked version of range scan. Designed to be called by public rangeScan method.
     */
    adl::size_t doRangeScan(
        const Node* node,
        const KeyType& lhs, 
        const KeyType& rhs, 
        void* data,
        bool (*collector) (void* data, const KeyType&, const DataType&)
    );


    /**
     * Rotate left.
     * 
     * @param node 
     * @exception If `node` dosen't have right subtree, undefined behaviour occurs.
     */
    void rotateLeft(Node* node);

    /**
     * Rotate right
     * 
     * @param node 
     * @exception If `node` dosen't have left subtree, undefined behaviour occurs
     */
    void rotateRight(Node* node);

    /**
     * 
     * 
     * @param node Child node of the two connected red nodes.
     * @exception If node is nullptr, UB would occur.
     */
    void rebalanceRedNode(Node* node);

    /**
     * 
     * 
     * @param node Lighter node.
     * @exception UB if node is not the lighter one.
     */
    void rebalanceChildren(Node* node);


    enum class LockType {
        READ, WRITE
    };

    void lock(LockType);
    void unlock(LockType);
    

protected:
    /**
     * Root node.
     */
    Node* root = nullptr;

    adl::Allocator* allocator = nullptr;

};


/* ---------------- Iterator ---------------- */


template<typename K, typename V>
class RedBlackTreeIterator {
    typedef RedBlackTreeIterator Self;

protected:
    RedBlackTree<K, V>::Node* root;  // Set to nullptr to disable this iterator.
    RedBlackTree<K, V>::Node* curr;

protected:
    void doOperatorPlusPlus() {
        if (root == nullptr)
            return;

        if (curr->rightChild) {
            curr = curr->rightChild;
            while (curr->leftChild)
                curr = curr->leftChild;
            return;
        }



        while (true) {

            if (curr->parent == nullptr) {
                root = nullptr;
                return;
            }

            if (curr->parent->rightChild == curr) {
                curr = curr->parent;
                continue;
            }

            curr = curr->parent;
            return;
        
        }


    }  // void doOperatorPlusPlus()

public:
    RedBlackTreeIterator(RedBlackTree<K, V>::Node* root) {
        this->root = this->curr = root;

        if (this->root == nullptr)
            return;

        while (this->curr->leftChild)
            this->curr = this->curr->leftChild;
    }

    adl::ref_pair<K, V> operator * () const { 
        return make_ref_pair(curr->key, curr->data); 
    }

    Self& operator ++ () {
        doOperatorPlusPlus();
        return *this;
    }

    Self operator ++ (int) {
        auto tmp = *this;
        doOperatorPlusPlus();
        return tmp;
    }

    friend bool operator == (const Self& a, const Self& b) {
        return (!a.root && !b.root) || (a.root == b.root && a.curr == b.curr);
    }

    friend bool operator != (const Self& a, const Self& b) {
        return !(a == b);
    }


};



/* ---------------- Impl ---------------- */

template<typename KeyType, typename DataType>
RedBlackTree<KeyType, DataType>::RedBlackTree(adl::Allocator* allocator)
{
    this->allocator = allocator;
}


template<typename KeyType, typename DataType>
RedBlackTree<KeyType, DataType>::~RedBlackTree()
{
    this->clear();
}


template<typename KeyType, typename DataType>
void RedBlackTree<KeyType, DataType>::clear()
{
    
    if (this->root != nullptr) {
        this->cleanup(this->root);
        this->root = nullptr;
    }
}


template<typename KeyType, typename DataType>
bool RedBlackTree<KeyType, DataType>::hasKey(const KeyType& queryKey)
{
    
    return !!locateNode(queryKey);
}


template<typename KeyType, typename DataType>
DataType& RedBlackTree<KeyType, DataType>::getData(const KeyType& key)
{
    
    auto node = locateNode(key);

    if (node) {
        return node->data;  // Warning: Race condition here.
    }


#if 0
    throw std::runtime_error("could not find your key in the object."); // key not found.
#else
    __die();
#endif

}



template<typename KeyType, typename DataType>
DataType& RedBlackTree<KeyType, DataType>::operator [] (const KeyType& key)
{
  
    {
        auto node = locateNode(key);

        if (node) {
            return node->data;  // Warning: Race condition here.
        }
    }


    this->setData(key, DataType {});

    return locateNode(key)->data;

}





template<typename KeyType, typename DataType>
DataType RedBlackTree<KeyType, DataType>::copyData(const KeyType& key, const DataType* fallback) {

    auto node = locateNode(key);
    
    if (node) {
        return node->data;
    } else if (fallback) {
        return *fallback;
    }

#if 0
    throw std::runtime_error("could not find your key in the object."); // key not found.
#else
    __die();
#endif
}


template<typename KeyType, typename DataType>
RedBlackTree<KeyType, DataType>& RedBlackTree<KeyType, DataType>::setData(
    const KeyType& key, 
    const DataType& data
)
{


    Node* currentNode = root;
    Node* currentParent = nullptr;

    while (currentNode != nullptr) {
        if (key == currentNode->key) { // key found
            currentNode->data = data;
            return *this; // data updated. exit.
        }
        else {
            currentParent = currentNode;
            currentNode = (currentNode->key > key ? currentNode->leftChild : currentNode->rightChild);
        }
    }


    // now, we should create new node for data.

    /*
        now, currentNode points to nullptr, currentParent points to the last visited node, probably nullptr.
        new node is red, inserted to the end.
    */

    // create node
    currentNode = allocator->alloc<Node>();
    currentNode->parent = currentParent;
    currentNode->leftChild = nullptr;
    currentNode->rightChild = nullptr;
    currentNode->key = key;
    currentNode->data = data;

    // If tree is empty, just set new node as root.
    if (currentParent == nullptr) {
        currentNode->color = NodeColor::BLACK;
        this->root = currentNode;
        return *this;
    }

    
    // now, we should handle when tree is not empty.
    
    currentNode->color = NodeColor::RED;
    // bind new node to parent.
    if (currentParent->key > key) {
        currentParent->leftChild = currentNode;
    }
    else {
        currentParent->rightChild = currentNode;
    }

    this->rebalanceRedNode(currentNode);
    return *this;
}


template<typename KeyType, typename DataType>
RedBlackTree<KeyType, DataType>& RedBlackTree<KeyType, DataType>::removeKey(
    const KeyType& key,
    bool noExcept
)
{

    
    Node* currentNode = root;

    while (currentNode != nullptr) {
        if (key == currentNode->key) {
            break; // key found. matched.
        }
        else {
            currentNode = (currentNode->key > key ? currentNode->leftChild : currentNode->rightChild);
        }
    }

    if (currentNode == nullptr) {

        if (noExcept)
            return *this;
        else {
#if 0
            throw std::runtime_error("could not find your key in the object."); // key not found.
#else
            __die();
#endif
        }
    }


    // Otherwise, find an alternative node.
    // As long as there is at least one child, continue to search for an alternative node.
    while (currentNode->leftChild != nullptr || currentNode->rightChild != nullptr) {
        if (currentNode->rightChild != nullptr) {
            // current node has right child.
            // replace the original deleted node with its successor.
            // note that successor must has no left child.
            Node* replacementNode = currentNode->rightChild;
            while (replacementNode->leftChild != nullptr) {
                replacementNode = replacementNode->leftChild;
            }

            struct {
                Node* leftChild;
                Node* rightChild;
                NodeColor color;
                Node* parent;
            } currentNodeInfo = {
                    currentNode->leftChild, currentNode->rightChild,
                    currentNode->color, currentNode->parent
            }, replacementNodeInfo = {
                    replacementNode->leftChild, replacementNode->rightChild,
                    replacementNode->color, replacementNode->parent
            };

            // exchange color.
            currentNode->color = replacementNodeInfo.color;
            replacementNode->color = currentNodeInfo.color;

            // edit parent node.
            if (currentNodeInfo.parent != nullptr) {
                if (currentNodeInfo.parent->leftChild == currentNode) {
                    currentNodeInfo.parent->leftChild = replacementNode;
                }
                else {
                    currentNodeInfo.parent->rightChild = replacementNode;
                }
            }
            else {
                this->root = replacementNode;
            }

            // edit currentNode's left child's data.
            if (currentNodeInfo.leftChild != nullptr) {
                currentNodeInfo.leftChild->parent = replacementNode;
            }

            // edit replacementNode's right child's data (if has)
            if (replacementNodeInfo.rightChild != nullptr) {
                replacementNodeInfo.rightChild->parent = currentNode;
            }
            
            currentNode->leftChild = nullptr; 
            currentNode->rightChild = replacementNodeInfo.rightChild;
            if (replacementNodeInfo.parent == currentNode) {
                currentNode->parent = replacementNode;
            }
            else {
                currentNode->parent = replacementNodeInfo.parent;
            }

            replacementNode->leftChild = currentNodeInfo.leftChild;
            replacementNode->parent = currentNodeInfo.parent;
            if (currentNodeInfo.rightChild == replacementNode) {
                replacementNode->rightChild = currentNode;
            }
            else {
                replacementNode->rightChild = currentNodeInfo.rightChild;
            }

            if (currentNodeInfo.rightChild != replacementNode) {
                currentNodeInfo.rightChild->parent = replacementNode;
                replacementNodeInfo.parent->leftChild = currentNode;
            }
        } // if (currnode has right child)
        else { // curr child only have left child
        
            // note that prev node's right child must be nullptr.
            Node* replacementNode = currentNode->leftChild;
            while (replacementNode->rightChild != nullptr) {
                replacementNode = replacementNode->rightChild;
            }

            struct {
                Node* leftChild;
                Node* rightChild;
                NodeColor color;
                Node* parent;
            } currentNodeInfo = {
                    currentNode->leftChild, currentNode->rightChild,
                    currentNode->color, currentNode->parent
            }, replacementNodeInfo = {
                    replacementNode->leftChild, replacementNode->rightChild,
                    replacementNode->color, replacementNode->parent
            };

            // exchange color
            currentNode->color = replacementNodeInfo.color;
            replacementNode->color = currentNodeInfo.color;

            // edit parent node
            if (currentNodeInfo.parent != nullptr) {
                if (currentNodeInfo.parent->leftChild == currentNode) {
                    currentNodeInfo.parent->leftChild = replacementNode;
                }
                else {
                    currentNodeInfo.parent->rightChild = replacementNode;
                }
            }
            else {
                this->root = replacementNode;
            }


            if (currentNodeInfo.rightChild != nullptr) {
                currentNodeInfo.rightChild->parent = replacementNode;
            }

            if (replacementNodeInfo.leftChild != nullptr) {
                replacementNodeInfo.leftChild->parent = currentNode;
            }

            currentNode->rightChild = nullptr;
            currentNode->leftChild = replacementNodeInfo.leftChild;
            if (replacementNodeInfo.parent == currentNode) {
                currentNode->parent = replacementNode;
            }
            else {
                currentNode->parent = replacementNodeInfo.parent;
            }

            replacementNode->rightChild = currentNodeInfo.rightChild;
            replacementNode->parent = currentNodeInfo.parent;
            if (currentNodeInfo.leftChild == replacementNode) {
                replacementNode->leftChild = currentNode;
            }
            else {
                replacementNode->leftChild = currentNodeInfo.leftChild;
            }

            if (currentNodeInfo.leftChild != replacementNode) {
                currentNodeInfo.leftChild->parent = replacementNode;
                replacementNodeInfo.parent->rightChild = currentNode;
            }
        } 
    } // while (currentNode->leftChild != nullptr || currentNode->rightChild != nullptr)


    // now, node to be deleted has no child.

    if (currentNode == this->root) { // if deleting root
        this->root = nullptr;
        allocator->free(currentNode);
    } 
    else if (currentNode->color == NodeColor::RED) {
        if (currentNode == currentNode->parent->leftChild) {
            currentNode->parent->leftChild = nullptr;
        }
        else {
            currentNode->parent->rightChild = nullptr;
        }
        allocator->free(currentNode);
    } 
    else { 
        // target node's parent must exists.
        // because target is black, it must have sibling.
        Node* sibling = (currentNode->parent->leftChild != currentNode ?
            currentNode->parent->leftChild : currentNode->parent->rightChild);

        Node* currentparentParent = currentNode->parent;

        ChildSide siblingSideToparentParent = 
            (sibling == currentparentParent->leftChild ? ChildSide::LEFT : ChildSide::RIGHT);

        // now we can delete the node.
        if (currentparentParent->leftChild == currentNode) {
            currentparentParent->leftChild = nullptr;
        }
        else {
            currentparentParent->rightChild = nullptr;
        }
        
        allocator->free(currentNode);


        // Next, let's consider different scenarios.

        if (currentparentParent->color == NodeColor::RED) {
            // when parent is red, sibling must be red.

            if (sibling->leftChild != nullptr && sibling->rightChild != nullptr)
            {
                /*
                    X: Remove; R: Red; B: Black
                      R
                     / \
                    X   B
                       / \
                      R   R   Or her symmetrical counterpart
                */
                // sibling's children are red
                // note: If her black uncle has child, his child must be red
                // operation: rotate sibling, and rotate parent. set parent to black, sibling to red.
                sibling->color = NodeColor::RED;
                currentparentParent->color = NodeColor::BLACK;

                if (siblingSideToparentParent == ChildSide::RIGHT) {
                    sibling->rightChild->color = NodeColor::BLACK;
                    this->rotateLeft(currentparentParent);
                }
                else {
                    sibling->leftChild->color = NodeColor::BLACK;
                    this->rotateRight(currentparentParent);
                }
            }
            else if (siblingSideToparentParent == ChildSide::RIGHT && sibling->leftChild != nullptr)
            {
                /*
                    X: Remove; R: Red; B: Black
                      R
                     / \
                    X   B
                       /
                      R
                */

                currentparentParent->color = NodeColor::BLACK;
                this->rotateRight(sibling);
                this->rotateLeft(currentparentParent);
            }
            else if (siblingSideToparentParent == ChildSide::LEFT && sibling->rightChild != nullptr)
            {
                /*
                    X: Remove; R: Red; B: Black
                      R
                     / \
                    B   X
                     \
                      R
                */

                currentparentParent->color = NodeColor::BLACK;
                this->rotateLeft(sibling);
                this->rotateRight(currentparentParent);
            }
            else if (siblingSideToparentParent == ChildSide::RIGHT && sibling->rightChild != nullptr) {
                /*
                    X: Remove; R: Red; B: Black
                      R
                     / \
                    X   B
                         \
                          R
                */
                currentparentParent->color = NodeColor::BLACK;
                sibling->color = NodeColor::RED;
                sibling->rightChild->color = NodeColor::BLACK;
                this->rotateLeft(currentparentParent);
            }
            else if (siblingSideToparentParent == ChildSide::LEFT && sibling->leftChild != nullptr)
            {
                /*
                    X: Remove; R: Red; B: Black
                        R
                       / \
                      B   X
                     /
                    R
                */
                currentparentParent->color = NodeColor::BLACK;
                sibling->color = NodeColor::RED;
                sibling->leftChild->color = NodeColor::BLACK;
                this->rotateRight(currentparentParent);
            }
            else { // sibling has no child
                sibling->color = NodeColor::RED;
                currentparentParent->color = NodeColor::BLACK;
            }

        } // currentparentParent->color == NodeColor::RED
        else { // currentparentParent->color == NodeColor::BLACK
            if (sibling->color == NodeColor::BLACK
                && sibling->leftChild != nullptr && sibling->rightChild != nullptr)
            {
                // sibling is black, and it has two children. then these children must be red.
                if (siblingSideToparentParent == ChildSide::RIGHT) {
                    /*
                        X: Remove; R: Red; B: Black
                            B
                           / \
                          X   B
                             / \
                            R   R
                    */
                    sibling->leftChild->color = NodeColor::BLACK;
                    this->rotateRight(sibling);
                    this->rotateLeft(currentparentParent);
                }
                else {
                    /*
                        X: Remove; R: Red; B: Black
                            B
                           / \
                          B   X
                         / \
                        R   R
                    */
                    sibling->rightChild->color = NodeColor::BLACK;
                    this->rotateLeft(sibling);
                    this->rotateRight(currentparentParent);
                }
            } 
            else if (sibling->color == NodeColor::BLACK &&
                (sibling->leftChild != nullptr || sibling->rightChild != nullptr))
            {
                // sibling is black, and has one red child.
                if (siblingSideToparentParent == ChildSide::RIGHT && sibling->rightChild != nullptr) {
                    /*
                        X: Remove; R: Red; B: Black
                            B
                           / \
                          X   B
                               \
                                R
                    */
                    sibling->rightChild->color = NodeColor::BLACK;
                    this->rotateLeft(currentparentParent);
                }
                else if (siblingSideToparentParent == ChildSide::RIGHT && sibling->leftChild != nullptr)
                {
                    /*
                        X: Remove; R: Red; B: Black
                            B
                           / \
                          X   B
                             /
                            R
                    */
                    sibling->leftChild->color = NodeColor::BLACK;
                    this->rotateRight(sibling);
                    this->rotateLeft(currentparentParent);
                }
                else if (siblingSideToparentParent == ChildSide::LEFT && sibling->leftChild != nullptr)
                {
                    /*
                        X: Remove; R: Red; B: Black
                            B
                           / \
                          B   X
                         /
                        R
                    */
                    sibling->leftChild->color = NodeColor::BLACK;
                    this->rotateRight(currentparentParent);
                }
                else {
                    /*
                        X: Remove; R: Red; B: Black
                            B
                           / \
                          B   X
                           \
                            R
                    */
                    sibling->rightChild->color = NodeColor::BLACK;
                    this->rotateLeft(sibling);
                    this->rotateRight(currentparentParent);
                }
            } 
            else if (sibling->color == NodeColor::BLACK) {
                // black sibling without child
                /*
                       B          B
                      / \   ->   / \
                     X   B      X   R
                */
                if (currentparentParent->leftChild == nullptr) {
                    currentparentParent->rightChild->color = NodeColor::RED;
                    this->rebalanceChildren(currentparentParent);
                }
                else {
                    currentparentParent->leftChild->color = NodeColor::RED;
                    this->rebalanceChildren(currentparentParent);
                }
            } 
            else {
                // sibling is red. then it must has two black children.
                sibling->color = NodeColor::BLACK;
                currentparentParent->color = NodeColor::RED;
                if (siblingSideToparentParent == ChildSide::RIGHT) {
                    /*
                           B
                          / \
                         X   R
                            / \
                           B   B
                    */
                    this->rotateLeft(currentparentParent);
                    this->rotateLeft(currentparentParent);
                    if (currentparentParent->rightChild != nullptr) {
                        this->rebalanceRedNode(currentparentParent->rightChild);
                    }
                }
                else {
                    /*
                           B
                          / \
                         R   X
                        / \
                       B   B
                    */
                    this->rotateRight(currentparentParent);
                    this->rotateRight(currentparentParent);
                    if (currentparentParent->leftChild != nullptr) {
                        this->rebalanceRedNode(currentparentParent->leftChild);
                    }
                }
            }
        }
    }

    return *this;
}



template<typename KeyType, typename DataType>
adl::size_t RedBlackTree<KeyType, DataType>::size() {

    adl::size_t count = 0;
    for (const auto& _ : *this) {
        count++;
    }
    
    return count;
}


template<typename KeyType, typename DataType>
adl::size_t RedBlackTree<KeyType, DataType>::rangeScan(
    const KeyType& lhs, 
    const KeyType& rhs, 
    void* data,
    bool (*collector) (void* data, const KeyType&, const DataType&)
) {
    
    return this->root ? doRangeScan(this->root, lhs, rhs, data, collector) : 0;
}



template<typename KeyType, typename DataType>
void RedBlackTree<KeyType, DataType>::cleanup(Node* node)
{
    if (node->leftChild != nullptr) {
        cleanup(node->leftChild);
    }
    if (node->rightChild != nullptr) {
        cleanup(node->rightChild);
    }
    allocator->free(node);
}


template<typename KeyType, typename DataType>
RedBlackTree<KeyType, DataType>::Node* RedBlackTree<KeyType, DataType>::locateNode(const KeyType& key) {
    Node* currentNode = root;
    while (currentNode != nullptr) {
        if (key == currentNode->key) {
            return currentNode; // key found
        }
        else if (currentNode->key > key) {
            currentNode = currentNode->leftChild; // target is less than curr. search from left.
        }
        else { // key > currentNode->key
            currentNode = currentNode->rightChild; // target is more than curr. search from right.
        }
    }

    return nullptr;  // key not found.
}


template<typename KeyType, typename DataType>
adl::size_t RedBlackTree<KeyType, DataType>::doRangeScan(
    const Node* node,
    const KeyType& lhs, 
    const KeyType& rhs, 
    void* data,
    bool (*collector) (void* data, const KeyType&, const DataType&)
) {
    adl::size_t count = 0;

    auto left = node->leftChild;
    auto right = node->rightChild;

    if (left && node->key >= lhs)
        count += doRangeScan(left, lhs, rhs, data, collector);

    if (node->key >= lhs && rhs >= node->key) {
        count++;
        if (collector) {
            collector(data, node->key, node->data);
        }
    }

    if (right && rhs >= node->key)
        count += doRangeScan(right, lhs, rhs, data, collector);

    return count;
}


template<typename KeyType, typename DataType>
void RedBlackTree<KeyType, DataType>::rotateLeft(Node* node)
{
    Node* parent = node->parent;
    Node* targetRoot = node->rightChild;

    // rebind root.
    if (parent == nullptr) {
        this->root = targetRoot;
    }
    else {
        if (node == parent->leftChild) {
            parent->leftChild = targetRoot;
        }
        else {
            parent->rightChild = targetRoot;
        }
    }
    targetRoot->parent = parent;

    node->rightChild = targetRoot->leftChild; // child might be nullptr
    if (node->rightChild != nullptr) { // only when child is not null then we can bind parent node.
        node->rightChild->parent = node;
    }
    targetRoot->leftChild = node;
    node->parent = targetRoot;
}


template<typename KeyType, typename DataType>
void RedBlackTree<KeyType, DataType>::rotateRight(Node* node)
{
    Node* parent = node->parent;
    Node* targetRoot = node->leftChild;

    if (parent == nullptr) {
        this->root = targetRoot;
    }
    else {
        if (node == parent->leftChild) {
            parent->leftChild = targetRoot;
        }
        else {
            parent->rightChild = targetRoot;
        }
    }
    targetRoot->parent = parent;

    node->leftChild = targetRoot->rightChild; // child might be nullptr
    if (node->leftChild != nullptr) { 
        node->leftChild->parent = node;
    }
    targetRoot->rightChild = node;
    node->parent = targetRoot;
}


template<typename KeyType, typename DataType>
void RedBlackTree<KeyType, DataType>::rebalanceRedNode(Node* node)
{
    Node* currentNode = node;
    
    // Only when the current node is red is it possible to violate the red-black tree rule 
    // when the parent node is also red, thus requiring a repair operation.
    while (currentNode->color == NodeColor::RED) {
        Node* currentparentParent = currentNode->parent;
        if (currentparentParent == nullptr) {
            // currentNode is root
            currentNode->color = NodeColor::BLACK;
            break;
        }
        else if (currentparentParent->color == NodeColor::BLACK) {
            // parent is black. no problem!
            break;
        }


        // Arriving here indicates that both the parent and target nodes are colored red.

        // If a parent node is red, then it cannot be the root, so a grandparent must exist.

        Node* currentGrandpa = currentparentParent->parent;

        // find uncle (might be null)
        Node* uncle =
            (currentGrandpa->leftChild != currentparentParent ?
                currentGrandpa->leftChild : currentGrandpa->rightChild);

        if (uncle != nullptr && uncle->color == NodeColor::RED) {
            
            uncle->color = NodeColor::BLACK;
            currentparentParent->color = NodeColor::BLACK;
            currentGrandpa->color = NodeColor::RED;

            currentNode = currentGrandpa;
        }
        else {
            
            if (currentparentParent == currentGrandpa->leftChild) {
                if (currentNode == currentparentParent->leftChild) {
                    this->rotateRight(currentGrandpa);
                    // recolor
                    currentparentParent->color = NodeColor::BLACK;
                    currentGrandpa->color = NodeColor::RED;
                }
                else {
                    this->rotateLeft(currentparentParent);
                    this->rotateRight(currentGrandpa);
                    // recolor
                    currentGrandpa->color = NodeColor::RED;
                    currentNode->color = NodeColor::BLACK;
                }
            } // if (currentparentParent == currentGrandpa->leftChild)
            else {

                if (currentNode == currentparentParent->rightChild) {
                    this->rotateLeft(currentGrandpa);
                    // recolor
                    currentparentParent->color = NodeColor::BLACK;
                    currentGrandpa->color = NodeColor::RED;
                }
                else {
                    this->rotateRight(currentparentParent);
                    this->rotateLeft(currentGrandpa);
                    // recolor
                    currentGrandpa->color = NodeColor::RED;
                    currentNode->color = NodeColor::BLACK;
                }
            } // if (currentparentParent != currentGrandpa->leftChild)

            return;
        }
    }
}


template<typename KeyType, typename DataType>
void RedBlackTree<KeyType, DataType>::rebalanceChildren(Node* node)
{
    Node* currentNode = node;

    while (currentNode != this->root) {
        Node* currentparentParent = currentNode->parent;
        Node* sibling = (currentparentParent->leftChild != currentNode ?
            currentparentParent->leftChild : currentparentParent->rightChild);
        ChildSide siblingSideToparentParent = 
            (sibling == currentparentParent->leftChild ? ChildSide::LEFT : ChildSide::RIGHT);

        if (currentparentParent->color == NodeColor::RED) {
            
            
            if (siblingSideToparentParent == ChildSide::RIGHT
                && sibling->leftChild->color == NodeColor::BLACK)
            {
                /*
                       R
                      / \
                     X   B
                        / \
                       B   ?
                */
                this->rotateLeft(currentparentParent);
                break; 
            }
            else if (siblingSideToparentParent == ChildSide::LEFT
                && sibling->rightChild->color == NodeColor::BLACK)
            {
                /*
                        R
                       / \
                      B   X
                     / \
                    ?   B
                */
                this->rotateRight(currentparentParent);
                break;
            }
            else if (siblingSideToparentParent == ChildSide::RIGHT
                && sibling->rightChild->color == NodeColor::BLACK)
            {
                /*
                       R
                      / \
                     X   B
                        / \
                       R   B
                */
                currentparentParent->color = NodeColor::BLACK;
                sibling->color = NodeColor::RED;
                this->rebalanceRedNode(sibling->leftChild);
                break;
            }
            else if (siblingSideToparentParent == ChildSide::LEFT
                && sibling->leftChild->color == NodeColor::BLACK)
            {
                /*
                        R
                       / \
                      B   X
                     / \
                    B   R
                */
                currentparentParent->color = NodeColor::BLACK;
                sibling->color = NodeColor::RED;
                this->rebalanceRedNode(sibling->rightChild);
                break;
            }
            else { // sibling has two red child
                currentparentParent->color = NodeColor::BLACK;
                sibling->color = NodeColor::RED;
                if (siblingSideToparentParent == ChildSide::RIGHT) {
                    sibling->rightChild->color = NodeColor::BLACK;
                    this->rotateLeft(currentparentParent);
                }
                else {
                    sibling->leftChild->color = NodeColor::BLACK;
                    this->rotateRight(currentparentParent);
                }
                break;
            }
        } 
        else { // parent is black
            if (sibling->color == NodeColor::RED) { // sibling is red
                /*
                        B
                       / \
                      X   R
                         / \
                        B   B

                    or her symmatrical counterpart
                */
                currentparentParent->color = NodeColor::RED;
                sibling->color = NodeColor::BLACK;
                if (siblingSideToparentParent == ChildSide::RIGHT) {
                    
                    this->rotateLeft(currentparentParent);
                }
                else {
                    this->rotateRight(currentparentParent);
                }
            } 
            else { // sibling is black
                if (sibling->leftChild->color == NodeColor::BLACK 
                    && sibling->rightChild->color == NodeColor::BLACK)
                {
                    /*
                          B
                         / \
                        X   B
                           / \
                          B   B

                        or her symmatrical counterpart
                    */
                    sibling->color = NodeColor::RED;
                    currentNode = currentparentParent;
                }
                else if (siblingSideToparentParent == ChildSide::RIGHT 
                    && sibling->rightChild->color == NodeColor::RED) 
                {
                    /*
                          B
                         / \
                        X   B
                           / \
                          ?   R

                    */
                    sibling->rightChild->color = NodeColor::BLACK;
                    this->rotateLeft(currentparentParent);
                    break;
                }
                else if (siblingSideToparentParent == ChildSide::LEFT
                    && sibling->leftChild->color == NodeColor::RED)
                {
                    /*
                            B
                           / \
                          B   X
                         / \
                        R   ?

                    */
                    sibling->leftChild->color = NodeColor::BLACK;
                    this->rotateRight(currentparentParent);
                    break;
                }
                else if (siblingSideToparentParent == ChildSide::RIGHT) 
                {
                    /*
                          B
                         / \
                        X   B
                           / \
                          R   B

                    */
                    sibling->leftChild->color = NodeColor::BLACK;
                    this->rotateRight(sibling);
                    this->rotateLeft(currentparentParent);
                    break;
                }
                else 
                {
                    /*
                            B
                           / \
                          B   X
                         / \
                        B   R

                    */
                    sibling->rightChild->color = NodeColor::BLACK;
                    this->rotateLeft(sibling);
                    this->rotateRight(currentparentParent);
                    break;
                }
            } 
        } 
    }
}


}  // namespace adl

